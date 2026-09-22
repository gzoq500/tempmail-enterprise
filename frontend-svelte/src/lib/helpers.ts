export interface EmailView {
  kind: 'html' | 'text' | 'empty';
  html?: string;
  text?: string;
}

function decodeMimeWords(value: string): string {
  return value.replace(/=\?([^?]+)\?([BbQq])\?([^?]*)\?=/g, (_match, _charset, encoding, data) => {
    try {
      if (encoding.toLowerCase() === 'b') {
        const bytes = Uint8Array.from(atob(data), (char) => char.charCodeAt(0));
        return new TextDecoder().decode(bytes);
      }
      const decoded = data
        .replace(/_/g, ' ')
        .replace(/=([0-9A-Fa-f]{2})/g, (_hex: string, value: string) => String.fromCharCode(parseInt(value, 16)));
      return decodeURIComponent(escape(decoded));
    } catch {
      return data;
    }
  });
}

export function formatSender(raw: string): string {
  if (!raw) return 'Unknown';
  const decoded = decodeMimeWords(raw.trim());
  const displayName = decoded.match(/^\s*"?([^"<]+?)"?\s*</)?.[1]?.trim();
  if (displayName) return displayName;
  return decoded.match(/<([^>]+)>/)?.[1]?.trim() || decoded;
}

export function senderAddress(raw: string): string {
  if (!raw) return '';
  const decoded = decodeMimeWords(raw.trim());
  return decoded.match(/<([^>]+)>/)?.[1]?.trim() || decoded;
}

function sanitizeUrl(value: string): string {
  const trimmed = value.trim();
  if (/^(?:javascript|vbscript|data):/i.test(trimmed)) return '#';
  return trimmed;
}

export function sanitizeEmailHtml(raw: string): string {
  if (!raw) return '';
  const parser = new DOMParser();
  const document = parser.parseFromString(raw, 'text/html');

  document.querySelectorAll('script, iframe, frame, frameset, object, embed, form, input, button, textarea, select, base, meta[http-equiv="refresh"]').forEach((element) => element.remove());

  for (const element of Array.from(document.querySelectorAll('*'))) {
    for (const attribute of Array.from(element.attributes)) {
      const name = attribute.name.toLowerCase();
      if (name.startsWith('on') || name === 'srcdoc') {
        element.removeAttribute(attribute.name);
        continue;
      }
      if (['href', 'src', 'xlink:href', 'action', 'formaction'].includes(name)) {
        const safeValue = sanitizeUrl(attribute.value);
        if (safeValue === '#') element.setAttribute(attribute.name, safeValue);
      }
    }

    if (element.tagName === 'IMG') {
      const image = element as HTMLImageElement;
      const width = Number(image.getAttribute('width') || 0);
      const height = Number(image.getAttribute('height') || 0);
      const style = (image.getAttribute('style') || '').toLowerCase();
      if ((width > 0 && width <= 2) || (height > 0 && height <= 2) || /(?:width|height)\s*:\s*[012]px/.test(style)) {
        image.remove();
      }
    }
  }

  return document.body.innerHTML.trim();
}

function getShadowFitCss(): string {
  return ':host{display:block;max-width:100%;overflow-x:auto;overflow-y:visible;overflow-wrap:break-word;word-wrap:break-word;background:#fff;color:#111827;-webkit-overflow-scrolling:touch;overscroll-behavior-x:contain;touch-action:pan-x pan-y;}' +
    'img,video{max-width:100%;height:auto;}' +
    'a{word-break:break-word;color:#2563eb;}' +
    '*,*::before,*::after{animation:none!important;transition:none!important;}';
}

export function buildEmailDocument(html?: string, text?: string): EmailView {
  const rawHtml = (html || '').trim();
  if (rawHtml && /<[a-z]/i.test(rawHtml)) {
    const parser = new DOMParser();
    const parsed = parser.parseFromString(rawHtml, 'text/html');
    const senderStyles = Array.from(parsed.querySelectorAll('style')).map((style) => style.outerHTML).join('\n');
    const sanitizedBody = sanitizeEmailHtml(parsed.body.innerHTML || rawHtml);
    if (sanitizedBody) {
      const shadowCss = getShadowFitCss();
      return { kind: 'html', html: `<style>${shadowCss}</style>${senderStyles}${sanitizedBody}` };
    }
  }

  const rawText = (text || '').trim();
  if (rawText) {
    if (/<[a-z][\s\S]*>/i.test(rawText) && /<html|<body|<div|<table|<p[\s>]|<!DOCTYPE/i.test(rawText)) {
      return buildEmailDocument(rawText);
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
    '<a href="$1" target="_blank" rel="noopener noreferrer" style="color:#2563eb;word-break:break-all;">$1</a>'
  );
}

export function fmtDate(dateStr: string, opts?: Intl.DateTimeFormatOptions, locale = 'id-ID'): string {
  return new Date(dateStr).toLocaleString(locale, opts || { hour: '2-digit', minute: '2-digit', day: 'numeric', month: 'short' });
}
