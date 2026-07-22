const API_BASE = '/api';

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

export async function generateAlias(): Promise<Alias> {
  const res = await fetch(`${API_BASE}/alias`, { method: 'POST' });
  if (!res.ok) throw new Error('Failed to generate alias');
  return res.json();
}

export async function getAliases(): Promise<{ aliases: Alias[] }> {
  const res = await fetch(`${API_BASE}/aliases`);
  if (!res.ok) throw new Error('Failed to get aliases');
  return res.json();
}

export async function getEmails(email: string, after?: number): Promise<{ emails: Email[] }> {
  const params = after ? `?after=${after}` : '';
  const res = await fetch(`${API_BASE}/emails/${encodeURIComponent(email)}${params}`);
  if (!res.ok) throw new Error('Failed to get emails');
  return res.json();
}

export async function getEmail(id: number): Promise<Email> {
  const res = await fetch(`${API_BASE}/email/${id}`);
  if (!res.ok) throw new Error('Failed to get email');
  return res.json();
}

export async function deleteAlias(email: string): Promise<void> {
  const res = await fetch(`${API_BASE}/alias/${encodeURIComponent(email)}`, { method: 'DELETE' });
  if (!res.ok) throw new Error('Failed to delete alias');
}

export async function checkNewEmails(email: string, after: number): Promise<{ emails: Email[]; count: number }> {
  const res = await fetch(`${API_BASE}/check/${encodeURIComponent(email)}?after=${after}`);
  if (!res.ok) throw new Error('Failed to check emails');
  return res.json();
}
