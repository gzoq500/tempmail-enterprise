const API = '/api';

export async function generateAlias(duration = '24h') {
  const res = await fetch(`${API}/alias`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ duration }) });
  if (!res.ok) throw new Error('Failed');
  return res.json();
}

export async function getAliases() {
  const res = await fetch(`${API}/aliases`);
  if (!res.ok) throw new Error('Failed');
  return res.json();
}

export async function getEmails(email, after) {
  const params = after ? `?after=${after}` : '';
  const res = await fetch(`${API}/emails/${encodeURIComponent(email)}${params}`);
  if (!res.ok) throw new Error('Failed');
  return res.json();
}

export async function deleteAlias(email) {
  await fetch(`${API}/alias/${encodeURIComponent(email)}`, { method: 'DELETE' });
}

export async function checkNewEmails(email, after) {
  const res = await fetch(`${API}/check/${encodeURIComponent(email)}?after=${after}`);
  if (!res.ok) throw new Error('Failed');
  return res.json();
}

export async function sendEmail(from, name, to, subject, body) {
  const res = await fetch(`${API}/send`, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ from, name, to, subject, body }) });
  return res.json();
}
