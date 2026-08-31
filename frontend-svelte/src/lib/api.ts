const API = '/api';
const STORAGE_KEY = 'tempmail.aliasKeys.v1';

export interface Alias {
  id: string;
  email: string;
  created_at: string;
  expires_at: string;
  email_count: number;
  api_key?: string;
}

export interface Email {
  id: number;
  from_address: string;
  to_address?: string;
  subject: string;
  body_text: string;
  body_html: string;
  received_at: string;
  is_read: boolean;
}

export interface EmailResponse { emails: Email[]; }
export interface AliasResponse { aliases: Alias[]; }
export interface CheckResponse { count: number; }
export interface SendResponse { success: boolean; error?: string; }

type StoredAliasKey = { email: string; api_key: string };

function loadKeys(): StoredAliasKey[] {
  if (typeof localStorage === 'undefined') return [];
  try {
    const value = JSON.parse(localStorage.getItem(STORAGE_KEY) || '[]');
    return Array.isArray(value)
      ? value.filter((item) => item && typeof item.email === 'string' && typeof item.api_key === 'string' && item.api_key.startsWith('temp-'))
      : [];
  } catch { return []; }
}

function saveKeys(items: StoredAliasKey[]): void {
  if (typeof localStorage === 'undefined') return;
  localStorage.setItem(STORAGE_KEY, JSON.stringify(items));
}

function storeAliasKey(email: string, apiKey: string): void {
  const items = loadKeys().filter((item) => item.email !== email && item.api_key !== apiKey);
  items.unshift({ email, api_key: apiKey });
  saveKeys(items);
}

function removeAliasKey(email: string): void {
  saveKeys(loadKeys().filter((item) => item.email !== email));
}

function keyForEmail(email: string): string {
  return loadKeys().find((item) => item.email === email)?.api_key || '';
}

function authHeaders(apiKey: string, json = false): HeadersInit {
  const headers: Record<string, string> = { 'X-API-Key': apiKey };
  if (json) headers['Content-Type'] = 'application/json';
  return headers;
}

export async function generateAlias(duration: string = '24h'): Promise<Alias> {
  const res = await fetch(`${API}/alias`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ duration })
  });
  if (!res.ok) throw new Error('Failed to generate alias');
  const alias: Alias = await res.json();
  if (!alias.api_key?.startsWith('temp-')) throw new Error('Server did not return an API key');
  storeAliasKey(alias.email, alias.api_key);
  return alias;
}

export async function generateCustomAlias(email: string, duration: string): Promise<Alias> {
  const res = await fetch(`${API}/alias`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ email, duration })
  });
  if (!res.ok) throw new Error('Failed to generate custom alias');
  const alias: Alias = await res.json();
  if (!alias.api_key?.startsWith('temp-')) throw new Error('Server did not return an API key');
  storeAliasKey(alias.email, alias.api_key);
  return alias;
}

export async function getAliases(): Promise<AliasResponse> {
  const aliases: Alias[] = [];
  const validKeys: StoredAliasKey[] = [];
  for (const item of loadKeys()) {
    try {
      const res = await fetch(`${API}/aliases`, { headers: authHeaders(item.api_key) });
      if (!res.ok) continue;
      const data: AliasResponse = await res.json();
      for (const alias of data.aliases || []) aliases.push({ ...alias, api_key: item.api_key });
      validKeys.push(item);
    } catch {}
  }
  saveKeys(validKeys);
  aliases.sort((a, b) => b.created_at.localeCompare(a.created_at));
  return { aliases };
}

export async function getEmails(email: string, after?: number): Promise<EmailResponse> {
  const apiKey = keyForEmail(email);
  const params = after ? `?after=${after}` : '';
  const res = await fetch(`${API}/emails/${encodeURIComponent(email)}${params}`, { headers: authHeaders(apiKey) });
  if (!res.ok) throw new Error('Failed to get emails');
  return res.json();
}

export async function deleteAlias(email: string): Promise<void> {
  const apiKey = keyForEmail(email);
  const res = await fetch(`${API}/alias/${encodeURIComponent(email)}`, { method: 'DELETE', headers: authHeaders(apiKey) });
  if (!res.ok) throw new Error('Failed to delete alias');
  removeAliasKey(email);
}

export async function checkNewEmails(email: string, after: number): Promise<CheckResponse> {
  const apiKey = keyForEmail(email);
  const res = await fetch(`${API}/check/${encodeURIComponent(email)}?after=${after}`, { headers: authHeaders(apiKey) });
  if (!res.ok) throw new Error('Failed to check emails');
  return res.json();
}

export async function waitForNewEmail(email: string, after: number, timeout = 30, signal?: AbortSignal): Promise<Email | null> {
  const apiKey = keyForEmail(email);
  const res = await fetch(`${API}/wait/${encodeURIComponent(email)}?after=${after}&timeout=${timeout}`, {
    signal,
    headers: authHeaders(apiKey)
  });
  if (res.status === 408) return null;
  if (!res.ok) throw new Error('Failed to wait for emails');
  return res.json();
}

export async function sendEmail(from: string, name: string, to: string, subject: string, body: string): Promise<SendResponse> {
  const apiKey = keyForEmail(from);
  const res = await fetch(`${API}/send`, {
    method: 'POST',
    headers: authHeaders(apiKey, true),
    body: JSON.stringify({ from, name, to, subject, body })
  });
  return res.json();
}

export function getAliasApiKey(email: string): string {
  return keyForEmail(email);
}
