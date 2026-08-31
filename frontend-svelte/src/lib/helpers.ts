export interface DurationOption {
  value: string;
  label: string;
}

export interface EmailView {
  kind: 'html' | 'text' | 'empty';
  html?: string;
  text?: string;
}

export function formatSender(raw: string): string {
  if (!raw) return 'Unknown';
  let s = raw.trim();
  // Decode MIME encoded words: =?charset?encoding?data?=
  s = s.replace(/=\?([^?]+)\?([BbQq])\?([^?]*)\?=/g, (_: string, _c: string, enc: string, data: string) => {
    try {
      if (enc === 'B' || enc === 'b') return atob(data);
      return data.replace(/_/g, ' ').replace(/=([0-9A-Fa-f]{2})/g, (_: string, h: string) => String.fromCharCode(parseInt(h, 16)));
    } catch { return data; }
  });
  // Extract display name: "Name" <email> or Name <email>
  const nameMatch = s.match(/^"?([^"<]+?)"?\s*</);
  if (nameMatch && nameMatch[1].trim()) return nameMatch[1].trim();
  // Extract email from <email> format
  const emailMatch = s.match(/<([^>]+)>/);
  const email = emailMatch ? emailMatch[1] : s;
  // Bare address with a personal-looking local part: show it as-is
  // (maria@example.com -> maria) — deriving from the domain would render
  // meaningless names like "Example" for ordinary senders.
  if (email.includes('@')) {
    const [local, domain] = email.split('@');
    if (!domain) return email;
    // Personal = plain name-like local part, NOT a machine/bounce address.
    // bounces-271002886-3829060717-style locals (long digit runs, bounces
    // prefix, VERP patterns) must be treated as machine senders.
    const isMachineLocal =
      /^(no-?reply|donotreply|noreply|postmaster|mailer-daemon|notifications?|info|support|admin|hello|hi|newsletter|news|promotions?|marketing|team|billing|security|account|accounts|service|updates?|automated|auto)$/i.test(local) ||
      /^bounces?[-_]/i.test(local) ||
      /[-_.]?\d{5,}/.test(local) ||
      (local.match(/\d/g) || []).length >= 7;
    const personal = /^[a-z][a-z0-9._-]{2,}$/i.test(local) && !isMachineLocal;
    if (personal) return local;
    // Machine sender: derive a recognizable brand name from the domain.
    // Drop generic/technical subdomains first so notify.gologin.com,
    // mail.github.com and bounce.linkedin.com all resolve to the brand.
    const TECH_SUBDOMAINS = /^(www|mail|notify|notification|notifications|bounce|bounces|no-?reply|noreply|email|e|em|m|news|newsletter|msg|message|send|sender|smtp|out|outbound|mx|mta|reply|auto|auto-reply|responder|campaign|track|tracking|click|link|img|images?|static|cdn|api|dev|stage|staging|test)\./i;
    let domainPart = domain.toLowerCase();
    let guard = 0;
    while (TECH_SUBDOMAINS.test(domainPart) && domainPart.includes('.', 4) && guard++ < 4) {
      domainPart = domainPart.replace(TECH_SUBDOMAINS, '');
    }
    const core = domainPart.replace(/\.(com|net|org|io|id|co|web\.id|my\.id|biz\.id|info|app|dev|ai|me|xyz)$/i, '');
    const parts = core.split('.').filter(Boolean);
    const last = parts[parts.length - 1] || core;
    if (last) {
      return last
        .split(/[-_]/)
        .filter(Boolean)
        .map((w) => w.charAt(0).toUpperCase() + w.slice(1))
        .join(' ');
    }
  }
  return email;
}

// Render policy: emails are displayed EXACTLY as received. Nothing is stripped
// or rewritten — scripts, forms, event handlers, styles, images and links all
// pass through untouched. Isolation is provided by the Shadow DOM root in
// App.svelte, not by sanitizing the payload here.

// Build the wrapped HTML email document for Shadow DOM rendering.
// Shadow DOM keeps sender styles isolated from the app (like an iframe)
// but lives in the same compositor tree as the page — no separate layer,
// no cross-frame rasterization stalls while scrolling on mobile.
const SHADOW_FIT_CSS =
  ':host{display:block;max-width:100%;overflow-wrap:break-word;word-wrap:break-word;background:#fff;}' +
  'img,video{max-width:100%!important;height:auto!important;}' +
  'table,td{max-width:100%!important;}' +
  '*,*::before,*::after{animation:none!important;transition:none!important;}';

export function buildEmailDocument(html?: string, text?: string): EmailView {
  const rawHtml = (html || '').trim();
  if (rawHtml && /<[a-z]/i.test(rawHtml)) {
    // Extract body content: everything inside <body>, or the whole HTML when
    // there is no body tag (fragments, truncated HTML).
    const bodyMatch = rawHtml.match(/<body[^>]*>([\s\S]*?)<\/body>/i);
    const bodyHtml = bodyMatch ? bodyMatch[1] : rawHtml.replace(/^[\s\S]*?<\/head>/i, '');
    // Collect ALL sender <style> blocks (head or body — emails inline them
    // in both places). Their CSS only applies inside the shadow root.
    const styleBlocks = rawHtml.match(/<style[^>]*>[\s\S]*?<\/style>/gi) || [];
    const senderStyles = styleBlocks.join('\n');
    return {
      kind: 'html',
      html: '<style>' + SHADOW_FIT_CSS + '</style>' + senderStyles + bodyHtml
    };
  }
  const rawText = (text || '').trim();
  if (rawText) {
    // Some senders put full HTML into the text part.
    if (/<[a-z][\s\S]*>/i.test(rawText) && /<html|<body|<div|<table|<p[\s>]|<!DOCTYPE/i.test(rawText)) {
      return buildEmailDocument(rawText, undefined);
    }
    return { kind: 'text', text: rawText };
  }
  return { kind: 'empty' };
}

export function renderPlainText(text: string): string {
  const escaped = text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/\r\n/g, '\n')
    .replace(/\r/g, '\n');
  return escaped.replace(
    /(https?:\/\/[^\s<>"']+)/g,
    '<a href="$1" target="_blank" rel="noopener" style="color:#1a73e8;word-break:break-all;">$1</a>'
  );
}

export function fmtDate(dateStr: string, opts?: Intl.DateTimeFormatOptions): string {
  return new Date(dateStr).toLocaleString('id-ID', opts || { hour: '2-digit', minute: '2-digit', day: 'numeric', month: 'short' });
}

export const DURATIONS: DurationOption[] = [
  { value: '1h', label: '1 Jam' },
  { value: '24h', label: '24 Jam' },
  { value: '7d', label: '7 Hari' },
  { value: '30d', label: '1 Bulan' },
  { value: 'forever', label: '∞ Tanpa Batas' },
];
