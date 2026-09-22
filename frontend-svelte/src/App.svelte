<script>
  import { onMount, onDestroy } from 'svelte';
  import {
    generateAlias,
    generateCustomAlias,
    getAliases,
    getEmails,
    deleteAlias,
    waitForNewEmail,
    sendEmail,
    getAliasApiKey,
    importAliasKey
  } from './lib/api';
  import { formatSender, senderAddress, buildEmailDocument, renderPlainText, fmtDate } from './lib/helpers';
  import { messages, durationOptions } from './lib/i18n';

  let aliases = [];
  let activeAlias = null;
  let emails = [];
  let selectedEmail = null;
  let lastEmailId = 0;
  let loading = false;
  let refreshing = false;
  let showToast = false;
  let toastText = '';
  let appError = '';
  let showSend = false;
  let showChange = false;
  let showOpen = false;
  let showDelete = false;
  let deleteTarget = '';
  let deleting = false;
  let revealKey = false;
  let duration = '24h';
  let emailDomain = 'routerssh.web.id';
  let sendFrom = '';
  let sendName = '';
  let sendTo = '';
  let sendSubject = '';
  let sendBody = '';
  let sendError = '';
  let sending = false;
  let changeUsername = '';
  let openEmailInput = '';
  let openKeyInput = '';
  let openError = '';
  let opening = false;
  let locale = 'id';
  let theme = 'system';
  let showThemeMenu = false;
  let now = Date.now();
  let clockTimer = null;
  let toastTimer = null;
  let pollingGeneration = 0;
  let pollingController = null;

  $: t = messages[locale];
  $: durations = durationOptions[locale];
  $: emailView = selectedEmail ? buildEmailDocument(selectedEmail.body_html, selectedEmail.body_text) : null;
  $: activeExpiry = activeAlias ? expiryText(activeAlias.expires_at, locale) : '';
  $: unreadCount = emails.filter((email) => !email.is_read).length;
  $: currentThemeLabel = theme === 'light' ? t.themeLight : theme === 'dark' ? t.themeDark : t.themeSystem;

  function setLocale(nextLocale) {
    locale = nextLocale;
    localStorage.setItem('tempmail.locale', nextLocale);
    document.documentElement.lang = nextLocale;
  }

  function restoreThemeTriggerFocus() {
    setTimeout(() => {
      const trigger = document.querySelector('[data-theme-trigger]');
      if (trigger instanceof HTMLElement) trigger.focus();
    }, 0);
  }

  function setTheme(nextTheme) {
    theme = ['light', 'dark'].includes(nextTheme) ? nextTheme : 'system';
    localStorage.setItem('tempmail.theme', theme);
    document.documentElement.dataset.theme = theme;
    showThemeMenu = false;
    restoreThemeTriggerFocus();
  }


  function localizedDate(value, options) {
    return fmtDate(value, options, locale === 'id' ? 'id-ID' : 'en-US');
  }

  function emailLocal(value) {
    const index = value.lastIndexOf('@');
    return index > 0 ? value.slice(0, index) : value;
  }

  function emailDomainPart(value) {
    const index = value.lastIndexOf('@');
    return index > 0 ? value.slice(index) : '';
  }

  function expiryText(value, currentLocale) {
    if (!value) return '';
    const copy = messages[currentLocale];
    if (value.startsWith('2099')) return copy.unlimited;
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) return '';
    const remaining = date.getTime() - now;
    if (remaining <= 0) return copy.expired;
    const hours = Math.ceil(remaining / 3_600_000);
    if (hours < 24) return currentLocale === 'id' ? `${hours} jam` : `${hours} ${hours === 1 ? 'hour' : 'hours'}`;
    const days = Math.ceil(hours / 24);
    return currentLocale === 'id' ? `${days} hari` : `${days} ${days === 1 ? 'day' : 'days'}`;
  }

  async function loadAliases() {
    try {
      aliases = (await getAliases()).aliases;
    } catch {
      appError = t.error;
    }
  }

  async function loadEmails(email) {
    try {
      const data = await getEmails(email);
      if (activeAlias?.email !== email) return;
      const readIds = new Set(emails.filter((item) => item.is_read).map((item) => item.id));
      if (selectedEmail?.is_read) readIds.add(selectedEmail.id);
      emails = data.emails.map((item) => readIds.has(item.id) ? { ...item, is_read: true } : item);
      if (data.emails.length) lastEmailId = Math.max(...data.emails.map((item) => item.id));
    } catch {
      appError = t.failedRefresh;
    }
  }

  async function openEmail(emailRow) {
    selectedEmail = { ...emailRow, is_read: true };
    if (!emailRow.is_read) {
      emails = emails.map((email) => email.id === emailRow.id ? { ...email, is_read: true } : email);
    }
    stopPolling();
    try {
      const key = getAliasApiKey(emailRow.to_address || activeAlias?.email || '');
      const response = await fetch(`/api/email/${emailRow.id}`, { headers: key ? { 'X-API-Key': key } : {} });
      if (!response.ok) throw new Error('Email request failed');
      const full = await response.json();
      emails = emails.map((email) => email.id === full.id ? { ...email, is_read: true } : email);
      if (selectedEmail?.id === full.id) selectedEmail = { ...full, is_read: true };
    } catch {
      appError = t.error;
    }
  }

  async function handleGenerate() {
    loading = true;
    appError = '';
    try {
      const alias = await generateAlias(duration);
      activateAlias(alias);
      await loadAliases();
      await loadEmails(alias.email);
      startPolling();
    } catch {
      appError = t.failedGenerate;
    } finally {
      loading = false;
    }
  }

  function activateAlias(alias) {
    activeAlias = alias;
    selectedEmail = null;
    emails = [];
    lastEmailId = 0;
    if (alias.email.split('@')[1]) emailDomain = alias.email.split('@')[1];
  }

  function showToastMessage(message = t.copied) {
    if (toastTimer) clearTimeout(toastTimer);
    toastText = message;
    showToast = true;
    toastTimer = setTimeout(() => {
      showToast = false;
      toastTimer = null;
    }, 2200);
  }

  async function copyValue(value) {
    if (!value) return;
    try {
      await navigator.clipboard.writeText(value);
      showToastMessage();
    } catch {
      appError = t.copyFailed;
    }
  }

  async function handleRefresh() {
    if (!activeAlias || refreshing) return;
    refreshing = true;
    appError = '';
    await loadEmails(activeAlias.email);
    setTimeout(() => { refreshing = false; }, 650);
  }

  function requestDelete(email) {
    deleteTarget = email;
    showDelete = true;
  }

  async function handleDelete() {
    if (!deleteTarget || deleting) return;
    deleting = true;
    try {
      await deleteAlias(deleteTarget);
      if (activeAlias?.email === deleteTarget) {
        activeAlias = null;
        emails = [];
        selectedEmail = null;
        stopPolling();
      }
      showDelete = false;
      deleteTarget = '';
      loadAliases();
    } catch {
      appError = t.error;
    } finally {
      deleting = false;
    }
  }

  function closeTopLayer() {
    if (showThemeMenu) {
      showThemeMenu = false;
      restoreThemeTriggerFocus();
      return;
    }
    if (showDelete) { showDelete = false; deleteTarget = ''; return; }
    if (showSend) { showSend = false; return; }
    if (showChange) { showChange = false; return; }
    if (showOpen) showOpen = false;
  }

  function handleGlobalKeydown(event) {
    if (event.key === 'Escape') closeTopLayer();
  }

  function handleGlobalPointerDown(event) {
    if (!showThemeMenu) return;
    const target = event.target;
    if (target instanceof Element && !target.closest('.theme-picker')) {
      showThemeMenu = false;
    }
  }

  function themeMenuFocus(node) {
    const items = () => Array.from(node.querySelectorAll('[role="menuitemradio"]'));
    requestAnimationFrame(() => {
      const active = node.querySelector('[aria-checked="true"]') || items()[0];
      if (active instanceof HTMLElement) active.focus();
    });
    const onKeydown = (event) => {
      const options = items();
      const current = options.indexOf(document.activeElement);
      if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
        event.preventDefault();
        const direction = event.key === 'ArrowDown' ? 1 : -1;
        const next = options[(current + direction + options.length) % options.length];
        if (next instanceof HTMLElement) next.focus();
      } else if (event.key === 'Home' || event.key === 'End') {
        event.preventDefault();
        const next = event.key === 'Home' ? options[0] : options[options.length - 1];
        if (next instanceof HTMLElement) next.focus();
      }
    };
    node.addEventListener('keydown', onKeydown);
    return { destroy() { node.removeEventListener('keydown', onKeydown); } };
  }

  async function handleCustomEmail() {
    loading = true;
    appError = '';
    try {
      const requested = changeUsername.trim() ? `${changeUsername.trim()}@${emailDomain}` : '';
      const alias = requested ? await generateCustomAlias(requested, duration) : await generateAlias(duration);
      activateAlias(alias);
      await loadAliases();
      await loadEmails(alias.email);
      startPolling();
      showChange = false;
      changeUsername = '';
    } catch {
      appError = t.failedGenerate;
    } finally {
      loading = false;
    }
  }

  function handleRandom() {
    const names = ['andika', 'ayu', 'bagas', 'citra', 'dimas', 'farah', 'galih', 'intan', 'nanda', 'raka', 'sari', 'tio', 'wahyu'];
    const chars = 'abcdefghijklmnopqrstuvwxyz0123456789';
    let suffix = '';
    for (let index = 0; index < 3; index += 1) suffix += chars[Math.floor(Math.random() * chars.length)];
    changeUsername = `${names[Math.floor(Math.random() * names.length)]}${suffix}`;
  }

  async function handleOpenInbox() {
    opening = true;
    openError = '';
    try {
      const alias = await importAliasKey(openEmailInput, openKeyInput);
      activateAlias(alias);
      await loadAliases();
      await loadEmails(alias.email);
      startPolling();
      showOpen = false;
      openEmailInput = '';
      openKeyInput = '';
    } catch (error) {
      openError = t.failedOpen;
    } finally {
      opening = false;
    }
  }

  async function handleSend() {
    sending = true;
    sendError = '';
    try {
      const result = await sendEmail(sendFrom, sendName, sendTo, sendSubject, sendBody);
      if (!result.success) throw new Error(result.error || t.failed);
      showSend = false;
      sendTo = '';
      sendSubject = '';
      sendBody = '';
      showToastMessage(t.emailSent);
    } catch (error) {
      sendError = error?.message || t.failed;
    } finally {
      sending = false;
    }
  }

  function selectAlias(alias) {
    activateAlias(alias);
    loadEmails(alias.email);
    startPolling();
  }

  async function pollForEmails(generation) {
    while (generation === pollingGeneration && activeAlias && !selectedEmail) {
      pollingController = new AbortController();
      try {
        const email = await waitForNewEmail(activeAlias.email, lastEmailId, 30, pollingController.signal);
        if (generation !== pollingGeneration || !activeAlias || selectedEmail) return;
        if (email) {
          emails = [email, ...emails.filter((item) => item.id !== email.id)];
          lastEmailId = Math.max(lastEmailId, email.id);
        }
      } catch (error) {
        if (generation !== pollingGeneration) return;
        if (error?.name !== 'AbortError') await new Promise((resolve) => setTimeout(resolve, 1500));
      }
    }
  }

  function startPolling() {
    stopPolling();
    pollForEmails(pollingGeneration);
  }

  function stopPolling() {
    pollingGeneration += 1;
    if (pollingController) {
      pollingController.abort();
      pollingController = null;
    }
  }

  function goBack() {
    selectedEmail = null;
    startPolling();
  }

  function mountEmailShadow(node, html) {
    const root = node.attachShadow({ mode: 'open' });
    const update = (content) => {
      root.innerHTML = content;
      for (const anchor of root.querySelectorAll('a[href]')) {
        anchor.setAttribute('target', '_blank');
        anchor.setAttribute('rel', 'noopener noreferrer');
      }
    };
    update(html);
    return { update, destroy() { root.innerHTML = ''; } };
  }

  function senderInitial(raw) {
    const clean = formatSender(raw).replace(/[^\p{L}\p{N}]/gu, '');
    return clean ? clean[0].toUpperCase() : '?';
  }

  function senderColor(raw) {
    let hash = 0;
    for (const character of formatSender(raw)) hash = character.charCodeAt(0) + ((hash << 5) - hash);
    return `hsl(${Math.abs(hash) % 360}, 42%, 40%)`;
  }

  function fitAddress(node) {
    let animationFrame;
    const fit = () => {
      cancelAnimationFrame(animationFrame);
      animationFrame = requestAnimationFrame(() => {
        const baseSize = 16;
        node.style.fontSize = `${baseSize}px`;
        const availableWidth = node.clientWidth;
        const requiredWidth = node.scrollWidth;
        const fittedSize = requiredWidth > availableWidth
          ? Math.max(10, Math.floor((baseSize * availableWidth / requiredWidth) * 10) / 10)
          : baseSize;
        node.style.fontSize = `${fittedSize}px`;
      });
    };
    const observer = new ResizeObserver(fit);
    observer.observe(node);
    fit();
    return {
      update: fit,
      destroy() {
        cancelAnimationFrame(animationFrame);
        observer.disconnect();
      }
    };
  }

  function modalFocus(node) {
    const previousFocus = document.activeElement;
    const main = node.closest('main');
    const backdrop = node.parentElement;
    const background = main
      ? Array.from(main.children).filter((element) => element !== backdrop)
      : [];
    for (const element of background) element.setAttribute('inert', '');

    const focusableSelector = 'button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), a[href], [tabindex]:not([tabindex="-1"])';
    const focusFirst = () => {
      const preferred = node.querySelector('input, textarea, select');
      const first = preferred || node.querySelector(focusableSelector);
      if (first instanceof HTMLElement) first.focus();
    };
    requestAnimationFrame(focusFirst);

    const trapFocus = (event) => {
      if (event.key !== 'Tab') return;
      const focusable = Array.from(node.querySelectorAll(focusableSelector))
        .filter((element) => element instanceof HTMLElement && element.offsetParent !== null);
      if (!focusable.length) return;
      const first = focusable[0];
      const last = focusable[focusable.length - 1];
      if (event.shiftKey && document.activeElement === first) {
        event.preventDefault();
        last.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first.focus();
      }
    };
    node.addEventListener('keydown', trapFocus);

    return {
      destroy() {
        node.removeEventListener('keydown', trapFocus);
        for (const element of background) element.removeAttribute('inert');
        if (previousFocus instanceof HTMLElement) previousFocus.focus();
      }
    };
  }

  onMount(() => {
    const savedLocale = localStorage.getItem('tempmail.locale');
    locale = savedLocale === 'en' ? 'en' : 'id';
    document.documentElement.lang = locale;
    theme = localStorage.getItem('tempmail.theme') || 'system';
    document.documentElement.dataset.theme = ['light', 'dark'].includes(theme) ? theme : 'system';
    loadAliases();
    clockTimer = setInterval(() => { now = Date.now(); }, 60_000);
  });

  onDestroy(() => {
    stopPolling();
    if (clockTimer) clearInterval(clockTimer);
    if (toastTimer) clearTimeout(toastTimer);
  });
</script>

<svelte:window on:keydown={handleGlobalKeydown} on:pointerdown={handleGlobalPointerDown} />

<svelte:head>
  <title>{t.pageTitle}</title>
  <meta name="description" content={t.metaDescription} />
  <meta name="theme-color" content="#0a0a0b" />
</svelte:head>

<main class="app-shell">
  <header class="site-header">
    <a class="brand" href="/" aria-label="TempMail">
      <img src="/tempmail-logo-orange.svg" alt="" />
      <span class="brand-copy"><strong><i>Temp</i>Mail</strong><small>{t.brandSubtitle}</small></span>
    </a>
    <div class="header-actions">
      {#if activeAlias}<span class="live-status"><i></i>{t.activeEyebrow}</span>{/if}
      <div class="theme-picker">
        <button type="button" class="theme-trigger" data-theme-trigger on:click={() => showThemeMenu = !showThemeMenu} aria-haspopup="menu" aria-expanded={showThemeMenu} aria-label={t.themeLabel}>
          {#if theme === 'dark'}<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 15.5A8 8 0 0 1 8.5 4 8.5 8.5 0 1 0 20 15.5Z"/></svg>{:else if theme === 'light'}<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M4.9 4.9l1.4 1.4m11.4 11.4 1.4 1.4M2 12h2m16 0h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>{:else}<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="3" y="4" width="18" height="13" rx="2"/><path d="M8 21h8m-4-4v4"/></svg>{/if}
          <span>{currentThemeLabel}</span>
        </button>
        {#if showThemeMenu}
          <div class="theme-menu" use:themeMenuFocus role="menu">
            <button type="button" role="menuitemradio" data-theme-choice="system" aria-checked={theme === 'system'} on:click={() => setTheme('system')}>{t.themeSystem}</button>
            <button type="button" role="menuitemradio" data-theme-choice="light" aria-checked={theme === 'light'} on:click={() => setTheme('light')}>{t.themeLight}</button>
            <button type="button" role="menuitemradio" data-theme-choice="dark" aria-checked={theme === 'dark'} on:click={() => setTheme('dark')}>{t.themeDark}</button>
          </div>
        {/if}
      </div>
      <div class="language-switch" aria-label={t.language}>
        <button type="button" class:is-active={locale === 'id'} on:click={() => setLocale('id')} aria-pressed={locale === 'id'}>ID</button>
        <button type="button" class:is-active={locale === 'en'} on:click={() => setLocale('en')} aria-pressed={locale === 'en'}>EN</button>
      </div>
    </div>
  </header>

  {#if appError}
    <div class="app-alert" role="alert"><span>{appError}</span><button on:click={() => appError = ''} aria-label={t.close}>×</button></div>
  {/if}

  {#if !activeAlias}
    <section class="landing-layout">
      <div class="landing-copy-block">
        <p class="eyebrow"><span aria-hidden="true"></span>{t.landingEyebrow}</p>
        <h1>{t.landingHeadline}</h1>
        <p class="landing-description">{t.landingDescription}</p>
        <div class="product-proof">
          <p><strong>{t.instantUseTitle}</strong><span>{t.instantUseCopy}</span></p>
          <p><strong>{t.localAccessTitle}</strong><span>{t.localAccessCopy}</span></p>
        </div>
      </div>

      <div class="create-panel">
        <div class="preview-window" aria-hidden="true">
          <div class="preview-topbar"><span class="preview-wordmark">TempMail</span><span class="ready-badge"><i></i>{t.ready}</span></div>
          <div class="preview-address"><small>{t.temporaryAddress}</small><strong>{t.sampleAddress}</strong></div>
          <div class="preview-messages">
            <div><i></i><span><b>{t.previewVerification}</b><small>{t.previewCodeReady}</small></span><time>{t.previewNow}</time></div>
            <div><i></i><span><b>{t.previewIncoming}</b><small>{t.previewReceived}</small></span><time>2m</time></div>
          </div>
          <div class="preview-caption"><strong>{t.previewTitle}</strong><span>{t.previewCopy}</span></div>
        </div>

        <div class="duration-panel">
          <div class="field-heading"><span class="field-label">{t.durationLabel}</span><span>{t.durationHelp}</span></div>
          <div class="duration-options" role="group" aria-label={t.durationLabel}>
            {#each durations as option}
              <button type="button" on:click={() => duration = option.value} aria-pressed={duration === option.value} class:is-selected={duration === option.value}>
                <span class="duration-full">{option.label}</span><span class="duration-short">{option.short}</span>
              </button>
            {/each}
          </div>
        </div>

        <button on:click={handleGenerate} disabled={loading} class="button button-primary button-large">
          <span>{loading ? t.generating : t.generateNew}</span>
          <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 12h14m-5-5 5 5-5 5"/></svg>
        </button>
        <button on:click={() => showOpen = true} class="text-action">{t.openWithKey}<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m9 18 6-6-6-6"/></svg></button>
      </div>
    </section>

    {#if aliases.length > 0}
      <section class="saved-section">
        <div class="section-heading"><div><p>{t.savedAddresses}</p><h2>{t.activeEmails}</h2><span>{t.savedHelp}</span></div><strong>{aliases.length}</strong></div>
        <div class="saved-grid">
          {#each aliases as alias (alias.id)}
            <article class="saved-row">
              <button class="saved-main" on:click={() => selectAlias(alias)} aria-label={`${t.openInbox}: ${alias.email}`}>
                <span class="saved-status"></span><span><strong>{alias.email}</strong><small>{alias.email_count} {t.emailCount} · {expiryText(alias.expires_at, locale)}</small></span>
              </button>
              <div class="saved-actions">
                <button on:click={() => copyValue(alias.email)} aria-label={t.copyEmailAria}><svg viewBox="0 0 24 24"><rect x="9" y="9" width="12" height="12" rx="2"/><path d="M15 9V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h4"/></svg></button>
                <button class="danger" on:click={() => requestDelete(alias.email)} aria-label={t.deleteMailbox}><svg viewBox="0 0 24 24"><path d="M4 7h16m-10 4v6m4-6v6m1-10V4H9v3m-2 0 1 14h8l1-14"/></svg></button>
              </div>
            </article>
          {/each}
        </div>
      </section>
    {/if}
  {:else}
    <section class="workspace-intro">
      <div><h1>{t.activeHeadline}</h1><p>{t.activeDescription}</p></div>
      <span class="expiry-badge"><small>{t.expires}</small>{activeExpiry}</span>
    </section>

    <section class="workspace-grid">
      <aside class="mailbox-sidebar">
        <div class="address-panel">
          <div class="panel-label"><span>{t.yourEmail}</span></div>
          <strong class="mailbox-address" use:fitAddress={activeAlias.email}><span>{emailLocal(activeAlias.email)}</span><span>{emailDomainPart(activeAlias.email)}</span></strong>
          <button on:click={() => copyValue(activeAlias.email)} class="button button-primary copy-button" aria-label={t.copyEmailAria}>
            <svg viewBox="0 0 24 24"><rect x="9" y="9" width="12" height="12" rx="2"/><path d="M15 9V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h4"/></svg>{t.copy}
          </button>
        </div>

        <div class="key-panel">
          <span class="key-icon"><svg viewBox="0 0 24 24"><rect x="5" y="10" width="14" height="10" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/></svg></span>
          <div class="key-content">
            <span><b>{t.automationKey}</b><small>{t.keyDescription}</small></span>
            <code>{revealKey ? (getAliasApiKey(activeAlias.email) || t.unavailable) : '••••••••••••••••••••••••'}</code>
            <div class="key-actions">
              <button class="key-reveal" on:click={() => revealKey = !revealKey}>{revealKey ? t.hideKey : t.showKey}</button>
              <button class="key-copy" on:click={() => copyValue(getAliasApiKey(activeAlias.email))} aria-label={t.tapToCopy}><svg viewBox="0 0 24 24"><rect x="9" y="9" width="12" height="12" rx="2"/><path d="M15 9V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h4"/></svg>{t.tapToCopy}</button>
            </div>
          </div>
        </div>

        <div class="mailbox-tools">
          <button on:click={() => showChange = true}><svg viewBox="0 0 24 24"><path d="M20 11a8 8 0 0 0-15.5-2M4 4v5h5m-5 4a8 8 0 0 0 15.5 2M20 20v-5h-5"/></svg>{t.changeAddress}</button>
          <button on:click={() => { showSend = true; sendFrom = activeAlias.email; }}><svg viewBox="0 0 24 24"><path d="M4 5h16v14H4zM4 8l8 6 8-6"/></svg>{t.sendEmail}</button>
          <button class="danger" on:click={() => requestDelete(activeAlias.email)}><svg viewBox="0 0 24 24"><path d="M4 7h16m-10 4v6m4-6v6m1-10V4H9v3m-2 0 1 14h8l1-14"/></svg>{t.delete}</button>
        </div>
      </aside>

      <div class="inbox-panel" class:has-content={emails.length > 0 || selectedEmail}>
        <header class="inbox-header">
          <div><div class="inbox-title-line"><h2>{t.inbox}</h2>{#if emails.length}<span>{emails.length} {t.messageTotal}</span>{/if}</div><p><span>{t.newMessagesAuto}</span>{#if unreadCount}<span class="unread-summary">{unreadCount} {t.unread}</span>{/if}</p></div>
          <button on:click={handleRefresh} disabled={refreshing} aria-label={t.refreshInbox} title={t.refreshInbox} class="refresh-button"><svg class:spin={refreshing} viewBox="0 0 24 24" aria-hidden="true"><path d="M20 11a8 8 0 0 0-15.5-2M4 4v5h5m-5 4a8 8 0 0 0 15.5 2M20 20v-5h-5"/></svg></button>
        </header>

        {#if selectedEmail}
          <article class="email-reader">
            <button on:click={goBack} class="back-button"><svg viewBox="0 0 24 24"><path d="m15 19-7-7 7-7"/></svg>{t.back}</button>
            <header class="email-header"><h2>{selectedEmail.subject || t.noSubject}</h2><div class="sender-row"><span class="sender-avatar" style="background:{senderColor(selectedEmail.from_address)}">{senderInitial(selectedEmail.from_address)}</span><div><strong>{formatSender(selectedEmail.from_address)}</strong>{#if senderAddress(selectedEmail.from_address) !== formatSender(selectedEmail.from_address)}<span class="sender-address">{senderAddress(selectedEmail.from_address)}</span>{/if}<small>{t.to} {activeAlias.email}</small><time>{localizedDate(selectedEmail.received_at, { weekday: 'long', hour: '2-digit', minute: '2-digit', day: 'numeric', month: 'long', year: 'numeric' })}</time></div></div></header>
            <div class="email-body">
              {#if emailView?.kind === 'html' && emailView.html}<div class="email-shadow-host" use:mountEmailShadow={emailView.html}></div>
              {:else if emailView?.kind === 'text' && emailView.text}<div class="email-text">{@html renderPlainText(emailView.text)}</div>
              {:else}<div class="empty-copy">{t.empty}</div>{/if}
            </div>
          </article>
        {:else if emails.length === 0}
          <div class="empty-state">
            <div class="empty-illustration"><svg viewBox="0 0 24 24"><path d="M4 6h16v12H4zM4 8l8 6 8-6"/></svg></div>
            <h3>{t.noEmail}</h3><p>{t.noEmailDescription}</p>
            <button on:click={() => copyValue(activeAlias.email)}>{t.copy}<svg viewBox="0 0 24 24"><rect x="9" y="9" width="12" height="12" rx="2"/><path d="M15 9V5a2 2 0 0 0-2-2H5a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h4"/></svg></button>
          </div>
        {:else}
          <div class="message-list">
            {#each emails as email (email.id)}
              <button on:click={() => openEmail(email)} class="message-row email-list-item" class:is-unread={!email.is_read}>
                <span class="sender-avatar small" style="background:{senderColor(email.from_address)}">{senderInitial(email.from_address)}</span>
                <span class="message-content"><span><strong>{formatSender(email.from_address)}</strong><time>{localizedDate(email.received_at)}</time></span><b>{email.subject || t.noSubject}</b>{#if !email.is_read}<i class="unread-label">{t.unread}</i>{/if}</span>
                <svg class="chevron" viewBox="0 0 24 24"><path d="m9 5 7 7-7 7"/></svg>
              </button>
            {/each}
            <div class="inbox-live-footer"><span><i></i>{t.autoActive}</span><strong>{t.waitingNext}</strong></div>
          </div>
        {/if}
      </div>
    </section>
  {/if}

  <footer class="site-footer">
    <span class="footer-brand">TempMail · RouterSSH</span>
    <div class="footer-meta"><a href="/docs">{t.apiDocs}</a><span>{locale === 'id' ? 'Alamat sementara, gratis untuk semua orang.' : 'Temporary address, free for everyone.'}</span></div>
  </footer>

  {#if showOpen}
    <div class="modal-backdrop" on:click|self={() => showOpen = false} role="presentation">
      <div class="modal" use:modalFocus role="dialog" aria-modal="true" aria-labelledby="open-title">
        <header><div><p>{t.openAccess}</p><h2 id="open-title">{t.openTitle}</h2></div><button on:click={() => showOpen = false} aria-label={t.close}>×</button></header>
        <p class="modal-description">{t.openDescription}</p>
        <form on:submit|preventDefault={handleOpenInbox}>
          <label for="open-email">{t.emailAddress}</label><input id="open-email" type="email" bind:value={openEmailInput} placeholder={t.emailPlaceholder} autocomplete="email" required />
          <label for="open-key">{t.apiKey}</label><input id="open-key" class="mono" type="text" bind:value={openKeyInput} placeholder="temp-xxxxxxxxxxxxxxxx" autocomplete="off" spellcheck="false" required />
          <small>{t.apiHelper}</small>
          {#if openError}<div class="form-error" role="alert">{openError}</div>{/if}
          <button type="submit" disabled={opening} class="button button-primary button-large">{opening ? t.opening : t.openInbox}</button>
        </form>
      </div>
    </div>
  {/if}

  {#if showChange}
    <div class="modal-backdrop" on:click|self={() => showChange = false} role="presentation">
      <div class="modal" use:modalFocus role="dialog" aria-modal="true" aria-labelledby="change-title">
        <header><div><p>{t.changeEyebrow}</p><h2 id="change-title">{t.changeTitle}</h2></div><button on:click={() => showChange = false} aria-label={t.close}>×</button></header>
        <p class="modal-description">{t.changeDescription}</p>
        <form on:submit|preventDefault={handleCustomEmail}>
          <label for="change-name">{t.username}</label><div class="joined-input"><input id="change-name" type="text" bind:value={changeUsername} placeholder={t.usernamePlaceholder} autocomplete="off" spellcheck="false" /><span>@{emailDomain}</span></div>
          <div class="modal-actions"><button type="button" on:click={handleRandom} class="button button-secondary">{t.random}</button><button type="submit" disabled={loading} class="button button-primary">{loading ? t.generating : t.apply}</button></div>
        </form>
      </div>
    </div>
  {/if}

  {#if showSend}
    <div class="modal-backdrop" on:click|self={() => showSend = false} role="presentation">
      <div class="modal modal-wide" use:modalFocus role="dialog" aria-modal="true" aria-labelledby="send-title">
        <header><div><p>{t.sendEmail}</p><h2 id="send-title">{t.sendTitle}</h2></div><button on:click={() => showSend = false} aria-label={t.close}>×</button></header>
        <p class="modal-description">{t.sendDescription}</p>
        <form on:submit|preventDefault={handleSend}>
          <div class="form-grid"><div><label for="send-from">{t.sender}</label><select id="send-from" bind:value={sendFrom}>{#each aliases as alias}<option value={alias.email}>{alias.email}</option>{/each}</select></div><div><label for="send-name">{t.senderName}</label><input id="send-name" type="text" bind:value={sendName} placeholder={t.senderNamePlaceholder} /></div></div>
          <label for="send-to">{t.destination}</label><input id="send-to" type="email" bind:value={sendTo} placeholder={t.destinationPlaceholder} required />
          <label for="send-subject">{t.subject}</label><input id="send-subject" type="text" bind:value={sendSubject} placeholder={t.subjectPlaceholder} />
          <label for="send-body">{t.messageBody}</label><textarea id="send-body" bind:value={sendBody} placeholder={t.messagePlaceholder} required></textarea>
          {#if sendError}<div class="form-error" role="alert">{sendError}</div>{/if}
          <button type="submit" disabled={sending} class="button button-primary button-large">{sending ? t.sending : t.send}</button>
        </form>
      </div>
    </div>
  {/if}

  {#if showDelete}
    <div class="modal-backdrop" on:click|self={() => { showDelete = false; deleteTarget = ''; }} role="presentation">
      <div class="modal delete-dialog" use:modalFocus role="alertdialog" aria-modal="true" aria-labelledby="delete-title" aria-describedby="delete-description">
        <span class="delete-icon"><svg viewBox="0 0 24 24"><path d="M4 7h16m-10 4v6m4-6v6m1-10V4H9v3m-2 0 1 14h8l1-14"/></svg></span>
        <h2 id="delete-title">{t.deleteTitle}</h2>
        <p id="delete-description" class="modal-description">{t.deleteDescription}</p>
        <code>{deleteTarget}</code>
        <div class="delete-actions"><button on:click={() => { showDelete = false; deleteTarget = ''; }} class="button button-secondary">{t.cancel}</button><button on:click={handleDelete} disabled={deleting} class="button button-danger">{deleting ? t.deleting : t.confirmDelete}</button></div>
      </div>
    </div>
  {/if}

  {#if showToast}<div class="toast" role="status"><svg viewBox="0 0 24 24"><path d="m5 13 4 4L19 7"/></svg>{toastText}</div>{/if}
</main>
