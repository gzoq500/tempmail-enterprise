const API = '/api';

export interface Alias {
  id: string;
  email: string;
  created_at: string;
  expires_at: string;
  email_count: number;
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

export interface EmailResponse {
  emails: Email[];
}

export interface AliasResponse {
  aliases: Alias[];
}

export interface CheckResponse {
  count: number;
}

export interface SendResponse {
  success: boolean;
  error?: string;
}

export async function generateAlias(duration: string = '24h'): Promise<Alias> {
  const res = await fetch(`${API}/alias`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ duration })
  });
  if (!res.ok) throw new Error('Failed to generate alias');
  return res.json();
}

export async function getAliases(): Promise<AliasResponse> {
  const res = await fetch(`${API}/aliases`);
  if (!res.ok) throw new Error('Failed to get aliases');
  return res.json();
}

export async function getEmails(email: string, after?: number): Promise<EmailResponse> {
  const params = after ? `?after=${after}` : '';
  const res = await fetch(`${API}/emails/${encodeURIComponent(email)}${params}`);
  if (!res.ok) throw new Error('Failed to get emails');
  return res.json();
}

export async function deleteAlias(email: string): Promise<void> {
  await fetch(`${API}/alias/${encodeURIComponent(email)}`, { method: 'DELETE' });
}

export async function checkNewEmails(email: string, after: number): Promise<CheckResponse> {
  const res = await fetch(`${API}/check/${encodeURIComponent(email)}?after=${after}`);
  if (!res.ok) throw new Error('Failed to check emails');
  return res.json();
}

export async function sendEmail(from: string, name: string, to: string, subject: string, body: string): Promise<SendResponse> {
  const res = await fetch(`${API}/send`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ from, name, to, subject, body })
  });
  return res.json();
}
