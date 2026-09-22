<script>
  import { onMount, onDestroy } from 'svelte';
  import { generateAlias, generateCustomAlias, getAliases, getEmails, deleteAlias, waitForNewEmail, sendEmail, getAliasApiKey, importAliasKey } from './lib/api';
  import { formatSender, buildEmailDocument, renderPlainText, fmtDate } from './lib/helpers';
  import { messages, durationOptions } from './lib/i18n';

  let aliases = [];
  let activeAlias = null;
  let emails = [];
  let selectedEmail = null;
  let lastEmailId = 0;
  let loading = false;
  let refreshing = false;
  let showToast = false;
  let showSend = false;
  let showChange = false;
  let duration = '24h';
  let emailDomain = 'routerssh.web.id';
  let interval = null;
  let sendFrom = '';
  let sendName = '';
  let sendTo = '';
  let sendSubject = '';
  let sendBody = '';
  let sendError = '';
  let sending = false;
  let changeUsername = '';
  let showOpen = false;
  let openEmailInput = '';
  let openKeyInput = '';
  let openError = '';
  let opening = false;
  let locale = 'id';
  $: t = messages[locale];
  $: durations = durationOptions[locale];
  $: emailView = selectedEmail ? buildEmailDocument(selectedEmail.body_html, selectedEmail.body_text) : null;

  function setLocale(nextLocale) {
    locale = nextLocale;
    localStorage.setItem('tempmail.locale', nextLocale);
    document.documentElement.lang = nextLocale;
  }
  function localizedDate(value, options) {
    return fmtDate(value, options, locale === 'id' ? 'id-ID' : 'en-US');
  }

  async function loadAliases() { try { aliases = (await getAliases()).aliases; } catch {} }
  async function loadEmails(email) {
    try {
      const d = await getEmails(email);
      // A slower response for the previous alias must never overwrite the
      // inbox after the user has already switched to another alias.
      if (activeAlias?.email !== email) return;
      emails = d.emails;
      if (d.emails.length > 0) lastEmailId = Math.max(...d.emails.map(e => e.id));
    } catch {}
  }
  // Inbox items arrive metadata-only; fetch the body when an email is opened.
  async function openEmail(emailRow) {
    selectedEmail = emailRow; stopPolling();
    if (emailRow.body_html !== undefined || emailRow.body_text !== undefined) return;
    try {
      const key = getAliasApiKey(emailRow.to_address || activeAlias?.email || '');
      const res = await fetch('/api/email/' + emailRow.id, { headers: key ? { 'X-API-Key': key } : {} });
      if (res.ok) {
        const full = await res.json();
        // Clicking email B while A is still loading must not leave B blank or
        // let A's slower response replace B.
        if (selectedEmail && selectedEmail.id === full.id) selectedEmail = full;
      }
    } catch {}
  }
  async function handleGenerate() { loading = true; try { const a = await generateAlias(duration); activeAlias = a; selectedEmail = null; emails = []; lastEmailId = 0; await loadAliases(); await loadEmails(a.email); startPolling(); } catch {} loading = false; }
  let toastTimer = null;
  function showToastMessage() {
    if (toastTimer) clearTimeout(toastTimer);
    showToast = true;
    toastTimer = setTimeout(() => { showToast = false; toastTimer = null; }, 2000);
  }
  function handleCopy(email) { navigator.clipboard.writeText(email || activeAlias?.email || ''); showToastMessage(); }
  function handleCopyApiKey() { handleCopy(getAliasApiKey(activeAlias?.email || '')); }
  async function handleRefresh() { if (!activeAlias) return; refreshing = true; await loadEmails(activeAlias.email); setTimeout(() => refreshing = false, 800); }
  async function handleDelete(email) { if (!confirm(t.deleteConfirm)) return; await deleteAlias(email); if (activeAlias?.email === email) { activeAlias = null; emails = []; selectedEmail = null; stopPolling(); } await loadAliases(); }
  async function handleCustomEmail() { loading = true; try { const em = changeUsername ? changeUsername + '@' + emailDomain : ''; const d = em ? await generateCustomAlias(em, duration) : await generateAlias(duration); if (d.email) { activeAlias = d; selectedEmail = null; emails = []; lastEmailId = 0; await loadAliases(); await loadEmails(d.email); startPolling(); } } catch {} loading = false; showChange = false; changeUsername = ''; }
  function handleRandom() { const names = ['andi','budi','citra','dewi','eko','fajar','gilang','hadi','indra','joko','kurnia','lukman','maman','nanda','opik','pratama','rahmat','sandi','taufik','udin','vicky','wahyu','yusuf','zainal','bayu','candra','dian','erwin','fauzi','gunawan']; const chars = 'abcdefghijklmnopqrstuvwxyz0123456789'; const name = names[Math.floor(Math.random() * names.length)]; let s = ''; for (let i = 0; i < 3; i++) s += chars[Math.floor(Math.random() * chars.length)]; changeUsername = name + s; }
  async function handleOpenInbox() {
    opening = true; openError = '';
    try {
      const alias = await importAliasKey(openEmailInput, openKeyInput);
      activeAlias = alias; selectedEmail = null; emails = []; lastEmailId = 0;
      await loadAliases(); await loadEmails(alias.email); startPolling();
      showOpen = false; openEmailInput = ''; openKeyInput = '';
    } catch (e) { openError = e?.message || t.failedOpen; }
    opening = false;
  }
  async function handleSend() { sending = true; sendError = ''; try { const r = await sendEmail(sendFrom, sendName, sendTo, sendSubject, sendBody); if (r.success) { showSend = false; showToastMessage(); } else sendError = r.error || t.failed; } catch { sendError = t.error; } sending = false; }
  function selectAlias(a) { activeAlias = a; selectedEmail = null; loadEmails(a.email); startPolling(); if (a.email.split('@')[1]) emailDomain = a.email.split('@')[1]; }
  let pollingGeneration = 0;
  let pollingController = null;
  async function pollForEmails(generation) {
    while (generation === pollingGeneration && activeAlias && !selectedEmail) {
      pollingController = new AbortController();
      try {
        const email = await waitForNewEmail(activeAlias.email, lastEmailId, 30, pollingController.signal);
        if (generation !== pollingGeneration || !activeAlias || selectedEmail) return;
        if (email) {
          emails = [email, ...emails.filter(e => e.id !== email.id)];
          lastEmailId = Math.max(lastEmailId, email.id);
        }
      } catch (error) {
        if (generation !== pollingGeneration) return;
        // Abort is expected when changing views. Network/auth/server failures
        // must back off; otherwise an immediate rejection creates a tight loop
        // that burns client CPU and floods the API.
        if (error?.name !== 'AbortError') {
          await new Promise((resolve) => setTimeout(resolve, 1500));
        }
      }
    }
  }
  function startPolling() {
    stopPolling();
    const generation = pollingGeneration;
    pollForEmails(generation);
  }
  function stopPolling() {
    pollingGeneration += 1;
    if (pollingController) { pollingController.abort(); pollingController = null; }
    if (interval) { clearInterval(interval); interval = null; }
  }
  function goBack() { selectedEmail = null; startPolling(); }

  // Shadow DOM email mount: replaces the old iframe. Sender styles are
  // encapsulated inside the shadow root (no leakage into the app), while
  // the content lives in the page's own compositor tree — scrolling past
  // it is native page scrolling with zero iframe rasterization cost.
  function mountEmailShadow(node, html) {
    const root = node.attachShadow({ mode: 'open' });
    const update = (content) => {
      root.innerHTML = content;
      // Magic links open in a new tab (replaces the old <base target>).
      for (const a of root.querySelectorAll('a[href]')) {
        a.setAttribute('target', '_blank');
        a.setAttribute('rel', 'noopener noreferrer');
      }
    };
    update(html);
    return {
      update,
      destroy() { root.innerHTML = ''; }
    };
  }

  // Gmail-style sender avatar helpers.
  function senderName(raw) { return formatSender(raw); }
  function senderInitial(raw) {
    const name = senderName(raw);
    const clean = name.replace(/[^\p{L}\p{N}]/gu, '').trim();
    return clean ? clean[0].toUpperCase() : '?';
  }
  function senderColor(raw) {
    let hash = 0;
    const name = senderName(raw);
    for (let i = 0; i < name.length; i++) hash = name.charCodeAt(i) + ((hash << 5) - hash);
    const hue = Math.abs(hash) % 360;
    return `hsl(${hue}, 45%, 42%)`;
  }

  onMount(() => {
    const savedLocale = localStorage.getItem('tempmail.locale');
    locale = savedLocale === 'en' ? 'en' : 'id';
    document.documentElement.lang = locale;
    loadAliases();
  });
  onDestroy(() => { stopPolling(); });
</script>

<main class="relative min-h-screen">
  <header class="site-masthead">
    <div class="site-brand">
      <div class="site-brand-mark" aria-hidden="true">
        <img src="/tempmail-logo-orange.svg" alt="" />
      </div>
      <div class="site-brand-copy">
        <div class="site-brand-name"><span>Temp</span>Mail</div>
        <div class="site-brand-subtitle">{t.brandSubtitle}</div>
      </div>
    </div>
    <div class="language-switch" aria-label={t.language}>
      <button type="button" class:is-active={locale === 'id'} on:click={() => setLocale('id')} aria-pressed={locale === 'id'}>ID</button>
      <button type="button" class:is-active={locale === 'en'} on:click={() => setLocale('en')} aria-pressed={locale === 'en'}>EN</button>
    </div>
  </header>

  <section class="landing-stage {activeAlias ? 'mailbox-active-stage' : ''} relative text-center {activeAlias ? 'pt-3 pb-2' : 'pt-4 pb-4'}">
    <div class="relative z-10 px-4 {activeAlias ? 'active-intro-wrap' : ''}">
      {#if !activeAlias}
        <div class="landing-hero">
          <div class="landing-hero-copy">
            <div class="landing-eyebrow"><span class="landing-eyebrow-line"></span>{t.landingEyebrow}</div>
            <h1 class="landing-main-heading">{t.landingHeadline}</h1>
            <p class="landing-copy">{t.landingDescription}</p>
          </div>

          <div class="landing-preview-composition" aria-hidden="true">
            <div class="landing-inbox-preview">
              <div class="preview-address-row">
                <div class="preview-avatar">T</div>
                <div class="preview-address-copy">
                  <span>{t.temporaryAddress}</span>
                  <strong>namaacak@routerssh.web.id</strong>
                </div>
                <div class="preview-active-dot"></div>
              </div>
              <div class="preview-message-row">
                <div class="preview-line preview-line-accent"></div>
                <div class="preview-line"></div>
                <div class="preview-time"></div>
              </div>
              <div class="preview-message-row preview-message-muted">
                <div class="preview-line preview-line-medium"></div>
                <div class="preview-line"></div>
                <div class="preview-time"></div>
              </div>
            </div>
            <div class="landing-preview-copy">
              <strong>{t.heroMessage}</strong>
              <span>{t.heroExpiry}</span>
            </div>
          </div>
        </div>

        <div class="landing-actions">
          <div class="duration-panel">
            <div class="duration-label">{t.durationLabel}</div>
            <div class="duration-options" role="group" aria-label={t.durationLabel}>
            {#each durations as opt}
              <button on:click={() => duration = opt.value} aria-pressed={duration === opt.value} class="duration-option {duration === opt.value ? 'is-selected' : ''}">{opt.label}</button>
            {/each}
            </div>
          </div>
          <button on:click={handleGenerate} disabled={loading} class="landing-primary-button">
            <span>{loading ? t.generating : t.generateNew}</span>
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M5 12h14m-5-5l5 5-5 5"/></svg>
          </button>
          <button on:click={() => showOpen = true} class="landing-secondary-button">
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M5 8.5A2.5 2.5 0 017.5 6h9A2.5 2.5 0 0119 8.5v7a2.5 2.5 0 01-2.5 2.5h-9A2.5 2.5 0 015 15.5v-7zM5 9l7 4.5L19 9"/></svg>
            <span>{t.openWithKey}</span>
          </button>
        </div>
      {:else}
        <h1 class="landing-subtitle active-mailbox-heading">{t.activeHeadline}</h1>
        <p class="landing-copy">{t.activeDescription}</p>
      {/if}
    </div>
  </section>

  <div class="max-w-lg w-full min-w-0 mx-auto px-4 pb-20 space-y-4 overflow-x-hidden">
    {#if activeAlias}
      <section class="mailbox-shell">
        <div class="mailbox-identity">
          <div class="mailbox-info-grid">
            <div class="mailbox-info-card mailbox-email-card">
              <span class="mailbox-label">{t.yourEmail}</span>
              <div class="mailbox-address" title={activeAlias.email}>{activeAlias.email}</div>
              <button on:click={() => handleCopy()} class="mailbox-copy-button" aria-label={t.copyEmailAria}>
                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><rect x="9" y="9" width="13" height="13" rx="2" stroke-width="1.8"/><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M5 15H4a2 2 0 01-2-2V4a2 2 0 012-2h9a2 2 0 012 2v1"/></svg>
                {t.copy}
              </button>
            </div>

            <div class="mailbox-info-card mailbox-key-card" role="button" tabindex="0" on:click={handleCopyApiKey} on:keydown={(e) => e.key === 'Enter' && handleCopyApiKey()}>
              <div class="mailbox-key-heading">
                <div class="mailbox-key-icon">
                  <svg class="w-3.5 h-3.5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><rect x="5" y="10" width="14" height="10" rx="2" stroke-width="1.8"/><path stroke-linecap="round" stroke-width="1.8" d="M8 10V7a4 4 0 018 0v3"/></svg>
                </div>
                <div class="mailbox-key-label">{t.automationKey} <span>· {t.tapToCopy}</span></div>
              </div>
              <div class="mailbox-key-value">{getAliasApiKey(activeAlias.email) || t.unavailable}</div>
            </div>
          </div>

          <div class="mailbox-toolbar">
            <button on:click={() => showChange = true} class="mailbox-tool">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M20 11a8.1 8.1 0 00-15.5-2M4 4v5h5m-5 4a8.1 8.1 0 0015.5 2M20 20v-5h-5"/></svg>
              {t.changeAddress}
            </button>
            <button on:click={() => { showSend = true; sendFrom = activeAlias?.email || ''; }} class="mailbox-tool">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M4 4h16v16H4zM4 7l8 6 8-6"/></svg>
              {t.sendEmail}
            </button>
            <button on:click={() => handleDelete(activeAlias.email)} class="mailbox-tool mailbox-tool-danger">
              <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M4 7h16m-10 4v6m4-6v6m1-10V4H9v3m-2 0l1 14h8l1-14"/></svg>
              {t.delete}
            </button>
          </div>
        </div>

        <div class="mailbox-inbox">
          <div class="mailbox-inbox-header">
            <div>
              <div class="flex items-center gap-2">
                <h2 class="mailbox-inbox-title">{t.inbox}</h2>
                {#if emails.length > 0}<span class="mailbox-count">{emails.length}</span>{/if}
              </div>
              <p class="mailbox-inbox-subtitle">{t.newMessagesAuto}</p>
            </div>
            <button on:click={handleRefresh} class="mailbox-refresh" aria-label={t.refreshInbox}>
              <svg class="w-4 h-4 {refreshing ? 'animate-spin' : ''}" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M20 11a8.1 8.1 0 00-15.5-2M4 4v5h5m-5 4a8.1 8.1 0 0015.5 2M20 20v-5h-5"/></svg>
            </button>
          </div>
        {#if selectedEmail}
          <div class="p-4">
            <button on:click={goBack} class="mailbox-back-button"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 19l-7-7 7-7"/></svg>{t.back}</button>
            <div class="border-b border-gray-800 pb-4 mb-4">
              <h2 class="text-xl font-bold text-gray-100 mb-3 pr-8">{selectedEmail.subject || t.noSubject}</h2>
              <div class="flex items-start gap-3">
                <div class="w-11 h-11 rounded-full flex items-center justify-center text-lg font-bold text-white flex-shrink-0" style="background:{senderColor(selectedEmail.from_address)}">
                  {senderInitial(selectedEmail.from_address)}
                </div>
                <div class="flex-1 min-w-0">
                  <div class="font-semibold text-gray-100 text-[15px] leading-tight truncate">{formatSender(selectedEmail.from_address)}</div>
                  <div class="text-xs text-gray-500 truncate">{t.to} {activeAlias?.email || t.me}</div>
                  <div class="text-xs text-gray-500 mt-0.5">{localizedDate(selectedEmail.received_at, { weekday: 'long', hour: '2-digit', minute: '2-digit', day: 'numeric', month: 'long', year: 'numeric' })}</div>
                </div>
              </div>
            </div>
            <div class="rounded-xl border border-gray-200 overflow-hidden" style="background:#fff;">
              {#if emailView?.kind === 'html' && emailView.html}
                <div class="email-shadow-host" use:mountEmailShadow={emailView.html}></div>
              {:else if emailView?.kind === 'text' && emailView.text}
                <div class="email-text" style="padding:16px;font-family:Arial,sans-serif;font-size:14px;line-height:1.6;color:#000;background:#fff;white-space:pre-wrap;">{@html renderPlainText(emailView.text)}</div>
              {:else}
                <div style="padding:16px;color:#999;font-style:italic">{t.empty}</div>
              {/if}
            </div>
          </div>
        {:else if emails.length === 0}
          <div class="mailbox-empty">
            <div class="mailbox-empty-art" aria-hidden="true">
              <svg class="w-7 h-7" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M4 6h16v12H4zM4 8l8 6 8-6"/></svg>
            </div>
            <p class="mailbox-empty-title">{t.noEmail}</p>
            <p class="mailbox-empty-copy">{t.noEmailDescription}</p>
          </div>
        {:else}
          <div class="mailbox-message-list">
            {#each emails as email (email.id)}
              <button on:click={() => openEmail(email)} class="mailbox-message email-list-item">
                <div class="mailbox-unread {email.is_read ? 'is-read' : ''}"></div>
                <div class="flex-1 min-w-0">
                  <div class="mailbox-message-top">
                    <div class="mailbox-message-sender">{formatSender(email.from_address)}</div>
                    <div class="mailbox-message-time">{localizedDate(email.received_at)}</div>
                  </div>
                  <div class="mailbox-message-subject">{email.subject || t.noSubject}</div>
                </div>
                <svg class="mailbox-message-chevron" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M9 5l7 7-7 7"/></svg>
              </button>
            {/each}
          </div>
        {/if}
        </div>
      </section>
    {/if}

    {#if !activeAlias && aliases.length > 0}
      <section class="saved-mailboxes">
        <div class="saved-mailboxes-heading">
          <div>
            <div class="saved-mailboxes-kicker"><span></span>{t.savedAddresses}</div>
            <h2 class="saved-mailboxes-title">{t.activeEmails}</h2>
            <p class="saved-mailboxes-copy">{t.savedHelp}</p>
          </div>
          <span class="saved-mailboxes-count" aria-label="{aliases.length} {t.activeEmailCount}">{aliases.length}</span>
        </div>
        <div class="saved-mailboxes-list">
          {#each aliases as alias (alias.id)}
            <div on:click={() => selectAlias(alias)} role="button" tabindex="0" on:keydown={(e) => e.key === 'Enter' && selectAlias(alias)} class="saved-mailbox-row">
              <div class="saved-mailbox-icon"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M4 7.5A2.5 2.5 0 016.5 5h11A2.5 2.5 0 0120 7.5v9a2.5 2.5 0 01-2.5 2.5h-11A2.5 2.5 0 014 16.5v-9zM4 8l8 5 8-5"/></svg></div>
              <div class="saved-mailbox-main">
                <p class="saved-mailbox-address">{alias.email}</p>
                <div class="saved-mailbox-meta"><span>{alias.email_count} {t.emailCount}</span><span class="saved-mailbox-separator">·</span><span>{alias.expires_at.startsWith('2099') ? t.unlimited : t.expires + ' ' + localizedDate(alias.expires_at)}</span></div>
              </div>
              <div class="saved-mailbox-actions">
                <button on:click|stopPropagation={() => handleCopy(alias.email)} aria-label={t.copyEmailAria} class="saved-mailbox-action"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><rect x="9" y="9" width="13" height="13" rx="2" stroke-width="1.8"/><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M5 15H4a2 2 0 01-2-2V4a2 2 0 012-2h9a2 2 0 012 2v1"/></svg></button>
                <button on:click|stopPropagation={() => handleDelete(alias.email)} aria-label={t.deleteAddressAria} class="saved-mailbox-action saved-mailbox-delete"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M4 7h16m-10 4v6m4-6v6m1-10V4H9v3m-2 0l1 14h8l1-14"/></svg></button>
                <svg class="saved-mailbox-chevron" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M9 5l7 7-7 7"/></svg>
              </div>
            </div>
          {/each}
        </div>
      </section>
    {/if}
  </div>

  {#if showOpen}
    <div role="presentation" class="open-inbox-backdrop" on:click|self={() => showOpen = false}>
      <div role="dialog" aria-modal="true" aria-labelledby="open-dialog-title" aria-describedby="open-dialog-description" class="open-inbox-dialog">
        <div class="open-inbox-glow" aria-hidden="true"></div>
        <header class="open-inbox-header">
          <div class="open-inbox-heading">
            <div class="open-inbox-eyebrow"><span></span>{t.openAccess}</div>
            <h3 id="open-dialog-title">{t.openTitle}</h3>
          </div>
          <button type="button" on:click={() => showOpen = false} class="open-inbox-close" aria-label={t.close}>
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M6 18L18 6M6 6l12 12"/></svg>
          </button>
        </header>

        <p id="open-dialog-description" class="open-inbox-description">{t.openDescription}</p>

        <form on:submit|preventDefault={handleOpenInbox} class="open-inbox-form">
          <div class="open-inbox-field">
            <label for="open-email">{t.emailAddress}</label>
            <div class="open-inbox-input-wrap">
              <svg class="open-inbox-field-icon" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.7" d="M4 6h16v12H4zM4 8l8 6 8-6"/></svg>
              <input id="open-email" type="email" bind:value={openEmailInput} placeholder="nama@routerssh.web.id" autocomplete="email" required />
            </div>
          </div>

          <div class="open-inbox-field">
            <label for="open-key">{t.apiKey}</label>
            <div class="open-inbox-input-wrap open-inbox-key-wrap">
              <svg class="open-inbox-field-icon" fill="none" stroke="currentColor" viewBox="0 0 24 24"><rect x="5" y="10" width="14" height="10" rx="2" stroke-width="1.7"/><path stroke-linecap="round" stroke-width="1.7" d="M8 10V7a4 4 0 018 0v3"/></svg>
              <input id="open-key" type="text" bind:value={openKeyInput} placeholder="temp-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" autocomplete="off" spellcheck="false" required />
            </div>
            <p class="open-inbox-helper">{t.apiHelper}</p>
          </div>

          {#if openError}<div class="open-inbox-error" role="alert">{openError}</div>{/if}

          <button type="submit" disabled={opening} class="open-inbox-submit">
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M4 7.5A2.5 2.5 0 016.5 5h11A2.5 2.5 0 0120 7.5v9a2.5 2.5 0 01-2.5 2.5h-11A2.5 2.5 0 014 16.5v-9zM4 8l8 5 8-5"/></svg>
            <span>{opening ? t.opening : t.openInbox}</span>
            <svg class="open-inbox-arrow" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M5 12h14m-5-5l5 5-5 5"/></svg>
          </button>
        </form>
      </div>
    </div>
  {/if}

  {#if showChange}
    <div role="presentation" class="change-address-backdrop" on:click|self={() => showChange = false}>
      <div role="dialog" aria-modal="true" aria-labelledby="change-dialog-title" aria-describedby="change-dialog-description" class="change-address-dialog">
        <header class="change-address-header">
          <div>
            <div class="change-address-eyebrow"><span></span>{t.changeEyebrow}</div>
            <h3 id="change-dialog-title">{t.changeTitle}</h3>
          </div>
          <button type="button" on:click={() => showChange = false} class="change-address-close" aria-label={t.close}><svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M6 18L18 6M6 6l12 12"/></svg></button>
        </header>
        <p id="change-dialog-description" class="change-address-description">{t.changeDescription}</p>

        <div class="change-address-field">
          <label for="change-username">{t.username}</label>
          <div class="change-address-composer">
            <input id="change-username" type="text" bind:value={changeUsername} placeholder={t.usernamePlaceholder} autocomplete="off" spellcheck="false" />
            <div class="change-address-domain"><span>@</span>{emailDomain}</div>
          </div>
          <span class="change-address-domain-label">{t.domain}: @{emailDomain}</span>
        </div>

        <div class="change-address-actions">
          <button type="button" on:click={handleRandom} class="change-address-random">
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M16 3h5v5m0-5l-6 6M8 21H3v-5m0 5l6-6M21 16v5h-5m5 0l-6-6M3 8V3h5M3 3l6 6"/></svg>
            {t.random}
          </button>
          <button type="button" on:click={handleCustomEmail} disabled={loading} class="change-address-apply">
            <span>{loading ? t.generating : t.apply}</span>
            <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.8" d="M5 12h14m-5-5l5 5-5 5"/></svg>
          </button>
        </div>
      </div>
    </div>
  {/if}

  {#if showSend}
    <div role="presentation" class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/60 backdrop-blur-sm" on:click|self={() => showSend = false}>
      <div role="dialog" aria-modal="true" aria-labelledby="send-dialog-title" class="bg-gray-900 border border-gray-700 w-full max-w-lg p-6 rounded-2xl max-h-[90vh] overflow-y-auto">
        <div class="flex items-center justify-between mb-6">
          <h3 id="send-dialog-title" class="text-xl font-bold text-gray-100">{t.sendTitle}</h3>
          <button on:click={() => showSend = false} aria-label={t.close} class="text-gray-400 hover:text-white"><svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12"/></svg></button>
        </div>
        <form on:submit|preventDefault={handleSend} class="space-y-4">
          <div><label for="send-from" class="block text-sm font-medium text-gray-400 mb-2">{t.sender}</label><select id="send-from" bind:value={sendFrom} class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm">{#each aliases as a}<option value={a.email}>{a.email}</option>{/each}</select></div>
          <div><label for="send-name" class="block text-sm font-medium text-gray-400 mb-2">{t.senderName}</label><input id="send-name" type="text" bind:value={sendName} placeholder={t.senderNamePlaceholder} class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm" /></div>
          <div><label for="send-to" class="block text-sm font-medium text-gray-400 mb-2">{t.destination}</label><input id="send-to" type="email" bind:value={sendTo} placeholder={t.destinationPlaceholder} required class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm" /></div>
          <div><label for="send-subject" class="block text-sm font-medium text-gray-400 mb-2">{t.subject}</label><input id="send-subject" type="text" bind:value={sendSubject} placeholder={t.subjectPlaceholder} class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm" /></div>
          <div><label for="send-body" class="block text-sm font-medium text-gray-400 mb-2">{t.messageBody}</label><textarea id="send-body" bind:value={sendBody} placeholder={t.messagePlaceholder} required class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm min-h-[120px] resize-y"></textarea></div>
          {#if sendError}<div class="text-red-400 text-sm bg-red-500/10 border border-red-500/20 rounded-xl p-3">{sendError}</div>{/if}
          <button type="submit" disabled={sending} class="w-full flex items-center justify-center gap-2 px-4 py-3 bg-orange-600 hover:bg-orange-500 text-white font-medium rounded-xl transition-all disabled:opacity-50"><svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 8l7.89 5.26a2 2 0 002.22 0L21 8M5 19h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z"/></svg>{sending ? t.sending : t.send}</button>
        </form>
      </div>
    </div>
  {/if}

  {#if showToast}
    <div class="fixed bottom-8 left-1/2 -translate-x-1/2 z-50 animate-bounce"><div class="flex items-center gap-2 px-4 py-2 bg-green-500 text-white rounded-xl shadow-lg"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7"/></svg>{t.copied}</div></div>
  {/if}
</main>

<style>
  .animate-spin { animation: spin 1s linear infinite; }
  @keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
  .animate-bounce { animation: bounce 1s infinite; }
  @keyframes bounce { 0%, 100% { transform: translate(-50%, 0); } 50% { transform: translate(-50%, -10px); } }
</style>
