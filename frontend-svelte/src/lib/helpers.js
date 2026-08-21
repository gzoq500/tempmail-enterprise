export function formatSender(raw) {
  if (!raw) return 'Unknown';
  let s = raw.trim();
  s = s.replace(/=\?([^?]+)\?([BbQq])\?([^?]*)\?=/g, (_, _c, enc, data) => {
    try {
      if (enc === 'B' || enc === 'b') return atob(data);
      return data.replace(/_/g, ' ').replace(/=([0-9A-Fa-f]{2})/g, (_, h) => String.fromCharCode(parseInt(h, 16)));
    } catch { return data; }
  });
  const nameMatch = s.match(/^"?([^"<]+?)"?\s*</);
  if (nameMatch && nameMatch[1].trim()) return nameMatch[1].trim();
  const emailMatch = s.match(/<([^>]+)>/);
  return emailMatch ? emailMatch[1] : s;
}

export function sanitizeHtml(raw) {
  if (!raw) return '';
  let h = raw.trim();
  h = h.replace(/<script[^>]*>[\s\S]*?<\/script>/gi, '');
  h = h.replace(/<noscript[^>]*>[\s\S]*?<\/noscript>/gi, '');
  h = h.replace(/<xml[^>]*>[\s\S]*?<\/xml>/gi, '');
  h = h.replace(/<form[^>]*>[\s\S]*?<\/form>/gi, '');
  h = h.replace(/<iframe[^>]*>[\s\S]*?<\/iframe>/gi, '');
  h = h.replace(/<!--[\s\S]*?-->/g, '');
  h = h.replace(/<\/?[vw]:[^>]*>/gi, '');
  h = h.replace(/<img[^>]*(?:width="?1"?|height="?1"?)[^>]*>/gi, '');
  const bodyMatch = h.match(/<body[^>]*>([\s\S]*)<\/body>/i);
  if (bodyMatch) h = bodyMatch[1];
  else if (h.includes('<body')) { const start = h.match(/<body[^>]*>([\s\S]*)/i); if (start) h = start[1]; }
  return h.trim() || raw.trim();
}

export function renderEmail(bodyHtml, bodyText) {
  const rawHtml = (bodyHtml || '').trim();
  const rawText = (bodyText || '').trim();
  if (rawHtml && rawHtml.length > 5) {
    const cleaned = sanitizeHtml(rawHtml);
    if (cleaned.length > 0) return { html: cleaned, isHtml: true };
    if (/<[a-z]/i.test(rawHtml)) return { html: rawHtml, isHtml: true };
  }
  if (rawText && /<[a-z][\s\S]*>/i.test(rawText)) {
    if (/<html|<body|<div|<table|<p[\s>]|<!DOCTYPE/i.test(rawText)) {
      const cleaned = sanitizeHtml(rawText);
      if (cleaned.length > 0) return { html: cleaned, isHtml: true };
    }
  }
  if (rawText && rawText.length > 0) {
    const escaped = rawText.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/\r\n/g, '\n').replace(/\r/g, '\n');
    const linked = escaped.replace(/(https?:\/\/[^\s<>"']+)/g, '<a href="$1" target="_blank" rel="noopener" style="color:#1a73e8;word-break:break-all;">$1</a>');
    return { html: linked, isHtml: false };
  }
  if (rawHtml && rawHtml.length > 0) return { html: rawHtml, isHtml: true };
  return { html: '', isHtml: false };
}

export function fmtDate(dateStr, opts) {
  return new Date(dateStr).toLocaleString('id-ID', opts || { hour: '2-digit', minute: '2-digit', day: 'numeric', month: 'short' });
}

export const DURATIONS = [
  { value: '1h', label: '1 Jam' },
  { value: '24h', label: '24 Jam' },
  { value: '7d', label: '7 Hari' },
  { value: '30d', label: '1 Bulan' },
  { value: 'forever', label: '∞ Tanpa Batas' },
];
