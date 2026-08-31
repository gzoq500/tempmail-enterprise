<script>
  import { onMount, onDestroy } from 'svelte';
  import { generateAlias, generateCustomAlias, getAliases, getEmails, deleteAlias, waitForNewEmail, sendEmail, getAliasApiKey } from './lib/api';
  import { formatSender, buildEmailDocument, renderPlainText, fmtDate, DURATIONS } from './lib/helpers';

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
  $: emailView = selectedEmail ? buildEmailDocument(selectedEmail.body_html, selectedEmail.body_text) : null;

  async function loadAliases() { try { aliases = (await getAliases()).aliases; } catch {} }
  async function loadEmails(email) { try { const d = await getEmails(email); emails = d.emails; if (d.emails.length > 0) lastEmailId = Math.max(...d.emails.map(e => e.id)); } catch {} }
  async function handleGenerate() { loading = true; try { const a = await generateAlias(duration); activeAlias = a; selectedEmail = null; emails = []; lastEmailId = 0; await loadAliases(); await loadEmails(a.email); startPolling(); } catch {} loading = false; }
  let toastTimer = null;
  function showToastMessage() {
    if (toastTimer) clearTimeout(toastTimer);
    showToast = true;
    toastTimer = setTimeout(() => { showToast = false; toastTimer = null; }, 2000);
  }
  function handleCopy(email) { navigator.clipboard.writeText(email || activeAlias?.email || ''); showToastMessage(); }
  async function handleRefresh() { if (!activeAlias) return; refreshing = true; await loadEmails(activeAlias.email); setTimeout(() => refreshing = false, 800); }
  async function handleDelete(email) { if (!confirm('Hapus email ini?')) return; await deleteAlias(email); if (activeAlias?.email === email) { activeAlias = null; emails = []; selectedEmail = null; stopPolling(); } await loadAliases(); }
  async function handleCustomEmail() { loading = true; try { const em = changeUsername ? changeUsername + '@' + emailDomain : ''; const d = em ? await generateCustomAlias(em, duration) : await generateAlias(duration); if (d.email) { activeAlias = d; selectedEmail = null; emails = []; lastEmailId = 0; await loadAliases(); await loadEmails(d.email); startPolling(); } } catch {} loading = false; showChange = false; changeUsername = ''; }
  function handleRandom() { const names = ['andi','budi','citra','dewi','eko','fajar','gilang','hadi','indra','joko','kurnia','lukman','maman','nanda','opik','pratama','rahmat','sandi','taufik','udin','vicky','wahyu','yusuf','zainal','bayu','candra','dian','erwin','fauzi','gunawan']; const chars = 'abcdefghijklmnopqrstuvwxyz0123456789'; const name = names[Math.floor(Math.random() * names.length)]; let s = ''; for (let i = 0; i < 3; i++) s += chars[Math.floor(Math.random() * chars.length)]; changeUsername = name + s; }
  async function handleSend() { sending = true; sendError = ''; try { const r = await sendEmail(sendFrom, sendName, sendTo, sendSubject, sendBody); if (r.success) { showSend = false; showToastMessage(); } else sendError = r.error || 'Gagal'; } catch { sendError = 'Error'; } sending = false; }
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
      } catch {}
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

  onMount(() => { loadAliases(); });
  onDestroy(() => { stopPolling(); });
</script>

<main class="min-h-screen">
  <section class="relative pt-10 pb-6 text-center">
    <div class="absolute inset-0 overflow-hidden pointer-events-none">
      <!-- Static radial gradients: visually identical to the old blur-2xl orbs
           but zero GPU filter cost while scrolling on mobile. -->
      <div class="absolute top-10 left-1/4 w-48 h-48" style="background:radial-gradient(circle,rgba(168,85,247,0.06),transparent 70%);"></div>
      <div class="absolute top-20 right-1/4 w-64 h-64" style="background:radial-gradient(circle,rgba(59,130,246,0.06),transparent 70%);"></div>
    </div>
    <div class="relative z-10 px-4">
      <h1 class="text-4xl md:text-5xl font-bold mb-3"><span class="bg-gradient-to-r from-purple-400 to-blue-400 bg-clip-text text-transparent">TempMail</span></h1>
      <p class="text-lg text-gray-300 mb-2">Email Sementara</p>
      <p class="text-sm text-gray-500 max-w-md mx-auto mb-6">Lindungi privasi Anda dengan email sementara. Generate email random, terima pesan langsung, tanpa registrasi.</p>
      {#if !activeAlias}
        <div class="space-y-3">
          <div class="flex flex-wrap justify-center gap-2">
            {#each DURATIONS as opt}
              <button on:click={() => duration = opt.value} class="px-3 py-1.5 text-xs font-medium rounded-lg transition-all {duration === opt.value ? 'bg-purple-600 text-white' : 'bg-gray-800/60 text-gray-400 hover:text-white'}">{opt.label}</button>
            {/each}
          </div>
          <button on:click={handleGenerate} disabled={loading} class="inline-flex items-center gap-2 px-8 py-3 bg-gradient-to-r from-purple-600 to-blue-600 hover:from-purple-500 hover:to-blue-500 text-white font-medium rounded-xl transition-all disabled:opacity-50">
            <svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 4v16m8-8H4"/></svg>
            {loading ? 'Generating...' : 'Generate Email Baru'}
          </button>
        </div>
      {/if}
    </div>
  </section>

  <div class="max-w-lg mx-auto px-4 pb-20 space-y-4">
    {#if activeAlias}
      <div class="bg-gray-900 rounded-2xl border border-gray-800 overflow-hidden">
        <div class="px-4 py-3 bg-gray-800/80 border-b border-gray-700/50 space-y-2.5">
          <div>
            <span class="block text-[10px] uppercase tracking-wide text-gray-500 mb-1">Email</span>
            <div class="flex items-center justify-between gap-2">
              <span class="text-sm font-mono text-purple-300 truncate">{activeAlias.email}</span>
              <button on:click={() => handleCopy()} title="Copy email" class="flex-shrink-0 p-1.5 -mr-1 rounded-md text-gray-400 hover:text-white hover:bg-gray-700/60 active:scale-95 transition-all"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 5H6a2 2 0 00-2 2v12a2 2 0 002 2h8a2 2 0 002-2v-12a2 2 0 00-2-2h-2"/><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 11h10a2 2 0 012 2v8a2 2 0 01-2 2h-10a2 2 0 01-2-2v-8a2 2 0 012-2z"/></svg></button>
            </div>
          </div>
          <div>
            <span class="block text-[10px] uppercase tracking-wide text-gray-500 mb-1">Automation API Key</span>
            <div class="flex items-center gap-1.5">
              <div class="flex-1 min-w-0 overflow-x-auto whitespace-nowrap scrollbar-thin" style="-webkit-overflow-scrolling:touch;">
                <code class="text-[11px] font-mono text-green-400">{getAliasApiKey(activeAlias.email) || 'Unavailable'}</code>
              </div>
              <button on:click={() => handleCopy(getAliasApiKey(activeAlias.email))} title="Copy API key" class="flex-shrink-0 p-1.5 -mr-1 rounded-md text-gray-400 hover:text-white hover:bg-gray-700/60 active:scale-95 transition-all"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 5H6a2 2 0 00-2 2v12a2 2 0 002 2h8a2 2 0 002-2v-12a2 2 0 00-2-2h-2"/><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 11h10a2 2 0 012 2v8a2 2 0 01-2 2h-10a2 2 0 01-2-2v-8a2 2 0 012-2z"/></svg></button>
            </div>
          </div>
        </div>
        <div class="grid grid-cols-2 gap-px bg-gray-700/50 m-4 rounded-xl overflow-hidden">
          <button on:click={() => showChange = true} class="flex items-center gap-2.5 px-4 py-3 bg-gray-800/80 hover:bg-gray-700 transition-colors text-sm font-medium text-gray-200"><svg class="w-4 h-4 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"/></svg>Change</button>
          <button on:click={() => handleCopy()} class="flex items-center gap-2.5 px-4 py-3 bg-gray-800/80 hover:bg-gray-700 transition-colors text-sm font-medium text-gray-200"><svg class="w-4 h-4 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 5H6a2 2 0 00-2 2v12a2 2 0 002 2h8a2 2 0 002-2v-12a2 2 0 00-2-2h-2"/><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 11h10a2 2 0 012 2v8a2 2 0 01-2 2h-10a2 2 0 01-2-2v-8a2 2 0 012-2z"/></svg>Copy</button>
          <button on:click={() => handleDelete(activeAlias.email)} class="flex items-center gap-2.5 px-4 py-3 bg-gray-800/80 hover:bg-gray-700 transition-colors text-sm font-medium text-gray-200"><svg class="w-4 h-4 text-gray-400" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"/></svg>Delete</button>
          <button on:click={handleRefresh} class="flex items-center gap-2.5 px-4 py-3 bg-gray-800/80 hover:bg-gray-700 transition-colors text-sm font-medium text-gray-200"><svg class="w-4 h-4 text-gray-400 {refreshing ? 'animate-spin' : ''}" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"/></svg>Refresh</button>
        </div>
      </div>
      <button on:click={() => { showSend = true; sendFrom = activeAlias?.email || ''; }} class="w-full flex items-center justify-center gap-2 px-4 py-3 bg-gray-800/80 hover:bg-gray-700 text-gray-200 font-medium rounded-xl border border-gray-700/50 transition-all"><svg class="w-4 h-4 text-green-400" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 8l7.89 5.26a2 2 0 002.22 0L21 8M5 19h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z"/></svg>Kirim Email</button>
      <div class="bg-gray-900 rounded-2xl border border-gray-800 overflow-hidden">
        <div class="flex items-center justify-between p-4 border-b border-gray-800/50">
          <h2 class="font-bold text-gray-100">Inbox</h2>
          <button on:click={handleRefresh} class="flex items-center gap-1 px-3 py-1.5 text-xs text-gray-400 hover:text-white bg-gray-800/50 hover:bg-gray-700/50 rounded-lg transition-all"><svg class="w-3.5 h-3.5 {refreshing ? 'animate-spin' : ''}" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"/></svg>Refresh</button>
        </div>
        {#if selectedEmail}
          <div class="p-4">
            <button on:click={goBack} class="flex items-center gap-2 text-purple-400 hover:text-purple-300 mb-4 transition-colors"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 19l-7-7 7-7"/></svg>Kembali</button>
            <div class="border-b border-gray-800 pb-4 mb-4">
              <h2 class="text-xl font-bold text-gray-100 mb-3 pr-8">{selectedEmail.subject || '(Tanpa subjek)'}</h2>
              <div class="flex items-start gap-3">
                <div class="w-11 h-11 rounded-full flex items-center justify-center text-lg font-bold text-white flex-shrink-0" style="background:{senderColor(selectedEmail.from_address)}">
                  {senderInitial(selectedEmail.from_address)}
                </div>
                <div class="flex-1 min-w-0">
                  <div class="font-semibold text-gray-100 text-[15px] leading-tight truncate">{formatSender(selectedEmail.from_address)}</div>
                  <div class="text-xs text-gray-500 truncate">kepada {activeAlias?.email || 'saya'}</div>
                  <div class="text-xs text-gray-500 mt-0.5">{fmtDate(selectedEmail.received_at, { weekday: 'long', hour: '2-digit', minute: '2-digit', day: 'numeric', month: 'long', year: 'numeric' })}</div>
                </div>
              </div>
            </div>
            <div class="rounded-xl border border-gray-200 overflow-hidden" style="background:#fff;">
              {#if emailView?.kind === 'html' && emailView.html}
                <div class="email-shadow-host" use:mountEmailShadow={emailView.html}></div>
              {:else if emailView?.kind === 'text' && emailView.text}
                <div class="email-text" style="padding:16px;font-family:Arial,sans-serif;font-size:14px;line-height:1.6;color:#000;background:#fff;white-space:pre-wrap;">{@html renderPlainText(emailView.text)}</div>
              {:else}
                <div style="padding:16px;color:#999;font-style:italic">(Kosong)</div>
              {/if}
            </div>
          </div>
        {:else if emails.length === 0}
          <div class="flex flex-col items-center justify-center py-16 px-4">
            <div class="w-20 h-20 bg-gray-800/50 rounded-2xl flex items-center justify-center mb-6"><svg class="w-10 h-10 text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="1.5" d="M3 8l7.89 5.26a2 2 0 002.22 0L21 8M5 19h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z"/></svg></div>
            <p class="font-bold text-gray-400 text-lg">Belum ada email</p>
            <p class="text-gray-600 text-sm mt-1">Email yang masuk akan muncul di sini</p>
          </div>
        {:else}
          <div class="divide-y divide-gray-800/30">
            {#each emails as email (email.id)}
              <button on:click={() => { selectedEmail = email; stopPolling(); }} class="w-full flex items-start gap-3 p-4 hover:bg-gray-800/30 cursor-pointer transition-colors text-left">
                <div class="w-2.5 h-2.5 rounded-full mt-1.5 flex-shrink-0 {email.is_read ? 'bg-gray-600' : 'bg-purple-400'}"></div>
                <div class="flex-1 min-w-0">
                  <div class="font-semibold text-gray-200 text-sm truncate">{formatSender(email.from_address)}</div>
                  <div class="text-gray-400 text-sm truncate">{email.subject || '(Tanpa subjek)'}</div>
                  <div class="text-gray-600 text-xs mt-1">{fmtDate(email.received_at)}</div>
                </div>
              </button>
            {/each}
          </div>
        {/if}
      </div>
    {/if}

    {#if !activeAlias && aliases.length > 0}
      <div>
        <h2 class="text-lg font-semibold text-gray-300 mb-3">Email Aktif</h2>
        <div class="space-y-3">
          {#each aliases as alias (alias.id)}
            <div on:click={() => selectAlias(alias)} role="button" tabindex="0" on:keydown={(e) => e.key === 'Enter' && selectAlias(alias)} class="bg-gray-900 hover:bg-gray-800 border border-gray-800 rounded-2xl p-4 transition-colors cursor-pointer">
              <div class="flex items-center gap-3">
                <div class="w-9 h-9 bg-gradient-to-br from-purple-500 to-blue-500 rounded-lg flex items-center justify-center flex-shrink-0"><svg class="w-4 h-4 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 8l7.89 5.26a2 2 0 002.22 0L21 8M5 19h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z"/></svg></div>
                <div class="flex-1 min-w-0">
                  <p class="font-mono font-semibold text-gray-200 text-sm break-all">{alias.email}</p>
                  <div class="flex items-center gap-2 mt-1">
                    <span class="text-xs px-2 py-0.5 rounded-full bg-blue-500/10 text-blue-400 border border-blue-500/20">{alias.email_count} email</span>
                    <span class="text-xs text-gray-500">{alias.expires_at.startsWith('2099') ? '∞ Tanpa Batas' : 'Exp: ' + fmtDate(alias.expires_at)}</span>
                  </div>
                </div>
                <div class="flex items-center gap-2">
                  <button on:click|stopPropagation={() => handleCopy(alias.email)} class="p-2 text-gray-400 hover:text-white rounded-lg transition-all"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z"/></svg></button>
                  <button on:click|stopPropagation={() => handleDelete(alias.email)} class="p-2 text-red-400 hover:text-red-300 rounded-lg transition-all"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"/></svg></button>
                  <svg class="w-5 h-5 text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 5l7 7-7 7"/></svg>
                </div>
              </div>
            </div>
          {/each}
        </div>
      </div>
    {/if}
  </div>

  {#if showChange}
    <div role="presentation" class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/60 backdrop-blur-sm" on:click|self={() => showChange = false}>
      <div role="dialog" aria-modal="true" aria-labelledby="change-dialog-title" class="bg-gray-900 border border-gray-700 w-full max-w-sm p-5 rounded-2xl">
        <div class="flex items-center justify-between mb-5">
          <h3 id="change-dialog-title" class="text-lg font-bold text-gray-100">Change Your Address</h3>
          <button on:click={() => showChange = false} class="text-gray-400 hover:text-white"><svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12"/></svg></button>
        </div>
        <div class="space-y-3">
          <input type="text" bind:value={changeUsername} placeholder="username (or leave empty)" class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm focus:outline-none focus:border-purple-500" />
          <div class="flex items-center gap-2 px-3 py-2.5 bg-gray-800/50 border border-gray-700/50 rounded-xl text-sm text-gray-400 font-mono">@{emailDomain}</div>
        </div>
        <div class="flex gap-3 mt-5">
          <button on:click={handleRandom} class="flex-1 px-4 py-2.5 bg-gray-800/80 hover:bg-gray-700 text-gray-200 text-sm font-medium rounded-xl border border-gray-700/50 transition-all">Random</button>
          <button on:click={handleCustomEmail} class="flex-1 flex items-center justify-center gap-2 px-4 py-2.5 bg-gradient-to-r from-blue-600 to-indigo-600 text-white text-sm font-medium rounded-xl transition-all"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7"/></svg>Apply</button>
        </div>
      </div>
    </div>
  {/if}

  {#if showSend}
    <div role="presentation" class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/60 backdrop-blur-sm" on:click|self={() => showSend = false}>
      <div role="dialog" aria-modal="true" aria-labelledby="send-dialog-title" class="bg-gray-900 border border-gray-700 w-full max-w-lg p-6 rounded-2xl max-h-[90vh] overflow-y-auto">
        <div class="flex items-center justify-between mb-6">
          <h3 id="send-dialog-title" class="text-xl font-bold text-gray-100">Kirim Email</h3>
          <button on:click={() => showSend = false} class="text-gray-400 hover:text-white"><svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M6 18L18 6M6 6l12 12"/></svg></button>
        </div>
        <form on:submit|preventDefault={handleSend} class="space-y-4">
          <div><label for="send-from" class="block text-sm font-medium text-gray-400 mb-2">Pilih Pengirim:</label><select id="send-from" bind:value={sendFrom} class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm">{#each aliases as a}<option value={a.email}>{a.email}</option>{/each}</select></div>
          <div><label for="send-name" class="block text-sm font-medium text-gray-400 mb-2">Nama Pengirim:</label><input id="send-name" type="text" bind:value={sendName} placeholder="Contoh: RouterSSH Support" class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm" /></div>
          <div><label for="send-to" class="block text-sm font-medium text-gray-400 mb-2">Email Tujuan:</label><input id="send-to" type="email" bind:value={sendTo} placeholder="tujuan@gmail.com" required class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm" /></div>
          <div><label for="send-subject" class="block text-sm font-medium text-gray-400 mb-2">Subjek:</label><input id="send-subject" type="text" bind:value={sendSubject} placeholder="Subjek email" class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm" /></div>
          <div><label for="send-body" class="block text-sm font-medium text-gray-400 mb-2">Isi Pesan:</label><textarea id="send-body" bind:value={sendBody} placeholder="Tulis pesan..." required class="w-full px-4 py-2.5 bg-gray-800 border border-gray-700 rounded-xl text-gray-200 text-sm min-h-[120px] resize-y"></textarea></div>
          {#if sendError}<div class="text-red-400 text-sm bg-red-500/10 border border-red-500/20 rounded-xl p-3">{sendError}</div>{/if}
          <button type="submit" disabled={sending} class="w-full flex items-center justify-center gap-2 px-4 py-3 bg-gradient-to-r from-green-600 to-emerald-600 text-white font-medium rounded-xl transition-all disabled:opacity-50"><svg class="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 8l7.89 5.26a2 2 0 002.22 0L21 8M5 19h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z"/></svg>{sending ? 'Mengirim...' : 'Kirim Email'}</button>
        </form>
      </div>
    </div>
  {/if}

  {#if showToast}
    <div class="fixed bottom-8 left-1/2 -translate-x-1/2 z-50 animate-bounce"><div class="flex items-center gap-2 px-4 py-2 bg-green-500 text-white rounded-xl shadow-lg"><svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7"/></svg>Email berhasil disalin!</div></div>
  {/if}
</main>

<style>
  .animate-spin { animation: spin 1s linear infinite; }
  @keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
  .animate-bounce { animation: bounce 1s infinite; }
  @keyframes bounce { 0%, 100% { transform: translate(-50%, 0); } 50% { transform: translate(-50%, -10px); } }
</style>
