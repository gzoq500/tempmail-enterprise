'use client';

import { useState, useEffect, useCallback, useRef } from 'react';
import { generateAlias, getAliases, getEmails, getEmail, deleteAlias, checkNewEmails, type Alias, type Email } from '@/lib/api';

// Send Email Modal
function SendEmailModal({ aliases, onClose, onSuccess }: { aliases: Alias[]; onClose: () => void; onSuccess: () => void }) {
  const [from, setFrom] = useState(aliases[0]?.email || '');
  const [name, setName] = useState('');
  const [to, setTo] = useState('');
  const [subject, setSubject] = useState('');
  const [body, setBody] = useState('');
  const [sending, setSending] = useState(false);
  const [error, setError] = useState('');

  const handleSend = async (e: React.FormEvent) => {
    e.preventDefault();
    setSending(true);
    setError('');
    try {
      const res = await fetch('/api/send', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ from, name, to, subject, body }) });
      const data = await res.json();
      if (data.success) { onSuccess(); onClose(); } else { setError(data.error || 'Gagal mengirim email'); }
    } catch (err) { setError('Terjadi kesalahan'); }
    setSending(false);
  };

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/60 backdrop-blur-sm" onClick={onClose}>
      <div className="card w-full max-w-lg p-6" onClick={(e) => e.stopPropagation()}>
        <div className="flex items-center justify-between mb-6">
          <h3 className="text-xl font-bold text-gray-100">Kirim Email</h3>
          <button onClick={onClose} className="btn-ghost"><svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M6 18L18 6M6 6l12 12" /></svg></button>
        </div>
        <form onSubmit={handleSend} className="space-y-4">
          <div><label className="block text-sm font-medium text-gray-400 mb-2">Pilih Pengirim:</label><select value={from} onChange={(e) => setFrom(e.target.value)} className="input">{aliases.map((a) => (<option key={a.id} value={a.email}>{a.email}</option>))}</select></div>
          <div><label className="block text-sm font-medium text-gray-400 mb-2">Nama Pengirim:</label><input type="text" value={name} onChange={(e) => setName(e.target.value)} placeholder="Contoh: RouterSSH Support" className="input" /></div>
          <div><label className="block text-sm font-medium text-gray-400 mb-2">Email Tujuan:</label><input type="email" value={to} onChange={(e) => setTo(e.target.value)} placeholder="tujuan@gmail.com" className="input" required /></div>
          <div><label className="block text-sm font-medium text-gray-400 mb-2">Subjek:</label><input type="text" value={subject} onChange={(e) => setSubject(e.target.value)} placeholder="Subjek email" className="input" /></div>
          <div><label className="block text-sm font-medium text-gray-400 mb-2">Isi Pesan:</label><textarea value={body} onChange={(e) => setBody(e.target.value)} placeholder="Tulis pesan..." className="input min-h-[120px] resize-y" required /></div>
          {error && <div className="text-red-400 text-sm bg-red-500/10 border border-red-500/20 rounded-xl p-3">{error}</div>}
          <button type="submit" disabled={sending} className="w-full btn-primary justify-center bg-gradient-to-r from-green-600 to-emerald-600 hover:from-green-500 hover:to-emerald-500">
            <svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M3 8l7.89 5.26a2 2 0 002.22 0L21 8M5 19h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z" /></svg>
            {sending ? 'Mengirim...' : 'Kirim Email'}
          </button>
        </form>
      </div>
    </div>
  );
}

// Copy Toast
function CopyToast({ show }: { show: boolean }) {
  if (!show) return null;
  return (
    <div className="fixed bottom-8 left-1/2 -translate-x-1/2 z-50 animate-bounce">
      <div className="flex items-center gap-2 px-4 py-2 bg-green-500 text-white rounded-xl shadow-lg shadow-green-500/25">
        <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M5 13l4 4L19 7" /></svg>
        Email berhasil disalin!
      </div>
    </div>
  );
}

// Email Detail
function EmailDetail({ email, onBack }: { email: Email; onBack: () => void }) {
  return (
    <div className="p-4">
      <button onClick={onBack} className="flex items-center gap-2 text-purple-400 hover:text-purple-300 mb-4 transition-colors">
        <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M15 19l-7-7 7-7" /></svg>
        Kembali
      </button>
      <div className="border-b border-gray-800 pb-4 mb-4">
        <div className="text-sm text-gray-500 mb-1">Dari: <span className="text-gray-300">{email.from_address}</span></div>
        <h2 className="text-xl font-bold text-gray-100">{email.subject || '(Tanpa subjek)'}</h2>
        <div className="text-xs text-gray-500 mt-1">{new Date(email.received_at).toLocaleString('id-ID', { weekday: 'long', hour: '2-digit', minute: '2-digit', day: 'numeric', month: 'long', year: 'numeric' })}</div>
      </div>
      <div className="email-content text-gray-300 text-sm leading-relaxed">
        {email.body_html ? (
          <div dangerouslySetInnerHTML={{ __html: email.body_html }} style={{ wordBreak: 'break-word' }} />
        ) : email.body_text ? (
          <div style={{ whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>
            {email.body_text.replace(/<[^>]*>/g, '').replace(/\n{3,}/g, '\n\n').trim()}
          </div>
        ) : (
          <div className="text-gray-500 italic">(Kosong)</div>
        )}
      </div>
    </div>
  );
}

// Main Page
export default function Home() {
  const [aliases, setAliases] = useState<any[]>([]);
  const [activeAlias, setActiveAlias] = useState<Alias | null>(null);
  const [emails, setEmails] = useState<Email[]>([]);
  const [selectedEmail, setSelectedEmail] = useState<Email | null>(null);
  const [lastEmailId, setLastEmailId] = useState(0);
  const [loading, setLoading] = useState(false);
  const [showToast, setShowToast] = useState(false);
  const [showSendModal, setShowSendModal] = useState(false);
  const [refreshing, setRefreshing] = useState(false);
  const intervalRef = useRef<NodeJS.Timeout | null>(null);

  const loadAliases = useCallback(async () => { try { setAliases((await getAliases()).aliases); } catch (err) { console.error(err); } }, []);
  const loadEmails = useCallback(async (email: string) => { try { const data = await getEmails(email); setEmails(data.emails); if (data.emails.length > 0) setLastEmailId(Math.max(...data.emails.map(e => e.id))); } catch (err) { console.error(err); } }, []);

  useEffect(() => { loadAliases(); }, [loadAliases]);
  useEffect(() => {
    if (activeAlias && !selectedEmail) {
      intervalRef.current = setInterval(async () => { try { const data = await checkNewEmails(activeAlias.email, lastEmailId); if (data.count > 0) loadEmails(activeAlias.email); } catch (err) { console.error(err); } }, 10000);
    }
    return () => { if (intervalRef.current) clearInterval(intervalRef.current); };
  }, [activeAlias, lastEmailId, selectedEmail, loadEmails]);

  const handleGenerate = async () => { setLoading(true); try { const alias = await generateAlias(); setActiveAlias(alias); setSelectedEmail(null); setEmails([]); setLastEmailId(0); loadAliases(); loadEmails(alias.email); } catch (err) { console.error(err); } setLoading(false); };
  const handleCopyEmail = () => { if (!activeAlias) return; navigator.clipboard.writeText(activeAlias.email); setShowToast(true); setTimeout(() => setShowToast(false), 2000); };
  const handleRefresh = () => {
    if (activeAlias) {
      setRefreshing(true);
      loadEmails(activeAlias.email);
      setTimeout(() => setRefreshing(false), 800);
    }
  };
  const handleDeleteAlias = async (email: string) => { if (!confirm('Hapus email ini?')) return; try { await deleteAlias(email); if (activeAlias?.email === email) { setActiveAlias(null); setEmails([]); setSelectedEmail(null); } loadAliases(); } catch (err) { console.error(err); } };

  return (
    <main className="min-h-screen">
      {/* Hero Header */}
      <section className="relative pt-10 pb-6 text-center overflow-hidden">
        <div className="absolute inset-0 overflow-hidden pointer-events-none">
          <div className="absolute top-10 left-1/4 w-48 h-48 bg-purple-500/5 rounded-full blur-2xl" />
          <div className="absolute top-20 right-1/4 w-64 h-64 bg-blue-500/5 rounded-full blur-2xl" />
        </div>
        <div className="relative z-10 px-4">
          <h1 className="text-4xl md:text-5xl font-bold mb-3">
            <span className="gradient-text">TempMail</span>
          </h1>
          <p className="text-lg text-gray-300 mb-2">Email Sementara</p>
          <p className="text-sm text-gray-500 max-w-md mx-auto mb-6">Lindungi privasi Anda dengan email sementara. Generate email random, terima pesan langsung, tanpa registrasi.</p>
          {!activeAlias && (
            <button onClick={handleGenerate} disabled={loading} className="btn-primary text-base px-8 py-3">
              <svg className="w-5 h-5" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 4v16m8-8H4" /></svg>
              {loading ? 'Generating...' : 'Generate Email Baru'}
            </button>
          )}
        </div>
      </section>

      <div className="max-w-lg mx-auto px-4 pb-20 space-y-4">
        {/* Active Email Card */}
        {activeAlias && (
          <div className="card p-5">
            <div className="flex items-center gap-3 mb-4">
              <div className="w-10 h-10 bg-gradient-to-br from-purple-500 to-blue-500 rounded-xl flex items-center justify-center flex-shrink-0">
                <svg className="w-5 h-5 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M3 8l7.89 5.26a2 2 0 002.22 0L21 8M5 19h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z" /></svg>
              </div>
              <div className="flex-1 min-w-0">
                <p className="text-xs text-gray-500">Email Aktif</p>
                <p className="font-mono font-bold text-purple-300 break-all text-sm">{activeAlias.email}</p>
              </div>
              <div className="flex items-center gap-2">
                <button onClick={handleCopyEmail} className="p-2 text-gray-400 hover:text-white hover:bg-gray-800/50 rounded-lg transition-all" title="Copy">
                  <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z" /></svg>
                </button>
                <button onClick={() => handleDeleteAlias(activeAlias.email)} className="p-2 text-red-400 hover:text-red-300 hover:bg-gray-800/50 rounded-lg transition-all" title="Hapus">
                  <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" /></svg>
                </button>
              </div>
            </div>
            <div className="flex items-center gap-2 text-xs text-gray-500">
              <span className="w-2 h-2 bg-green-400 rounded-full animate-pulse" />
              Auto-refresh setiap 10 detik
            </div>
          </div>
        )}

        {/* Action Buttons */}
        {activeAlias && (
          <div className="grid grid-cols-2 gap-3">
            <button onClick={handleRefresh} className="flex items-center justify-center gap-2 px-4 py-3 bg-gray-800/80 hover:bg-gray-700 text-gray-200 font-medium rounded-xl border border-gray-700/50 transition-all duration-200">
              <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" /></svg>
              Refresh
            </button>
            <button onClick={handleGenerate} disabled={loading} className="flex items-center justify-center gap-2 px-4 py-3 bg-gray-800/80 hover:bg-gray-700 text-gray-200 font-medium rounded-xl border border-gray-700/50 transition-all duration-200">
              <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 4v16m8-8H4" /></svg>
              {loading ? 'Loading...' : 'Change Email'}
            </button>
          </div>
        )}

        {/* Kirim Email */}
        {activeAlias && (
          <button onClick={() => setShowSendModal(true)} className="w-full flex items-center justify-center gap-2 px-4 py-3 bg-gray-800/80 hover:bg-gray-700 text-gray-200 font-medium rounded-xl border border-gray-700/50 transition-all duration-200">
            <svg className="w-4 h-4 text-green-400" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M3 8l7.89 5.26a2 2 0 002.22 0L21 8M5 19h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z" /></svg>
            Kirim Email
          </button>
        )}

        {/* Inbox Section */}
        {activeAlias && (
          <div className="card overflow-hidden">
            <div className="flex items-center justify-between p-4 border-b border-gray-800/50">
              <h2 className="font-bold text-gray-100">Inbox</h2>
              <button onClick={handleRefresh} className="flex items-center gap-1 px-3 py-1.5 text-xs text-gray-400 hover:text-white bg-gray-800/50 hover:bg-gray-700/50 rounded-lg transition-all">
                <svg className={`w-3.5 h-3.5 ${refreshing ? "animate-spin-continuous" : ""}`} fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" /></svg>
                Refresh
              </button>
            </div>
            {selectedEmail ? (
              <EmailDetail email={selectedEmail} onBack={() => setSelectedEmail(null)} />
            ) : emails.length === 0 ? (
              <div className="flex flex-col items-center justify-center py-16 px-4">
                <div className="relative w-20 h-20 mb-6">
                  <div className="w-20 h-20 bg-gray-800/50 rounded-2xl flex items-center justify-center">
                    <svg className="w-10 h-10 text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={1.5} d="M3 8l7.89 5.26a2 2 0 002.22 0L21 8M5 19h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z" /></svg>
                  </div>
                  <svg className="absolute -bottom-1 -right-1 w-8 h-8 text-purple-500" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" /></svg>
                </div>
                <p className="font-bold text-gray-400 text-lg">Belum ada email</p>
                <p className="text-gray-600 text-sm mt-1">Email yang masuk akan muncul di sini</p>
              </div>
            ) : (
              <div className="divide-y divide-gray-800/30">
                {emails.map((email) => (
                  <div key={email.id} onClick={() => setSelectedEmail(email)} className="flex items-start gap-3 p-4 hover:bg-gray-800/30 cursor-pointer transition-colors">
                    <div className={`w-2.5 h-2.5 rounded-full mt-1.5 flex-shrink-0 ${email.is_read ? 'bg-gray-600' : 'bg-purple-400'}`} />
                    <div className="flex-1 min-w-0">
                      <div className="font-semibold text-gray-200 text-sm truncate">{email.from_address}</div>
                      <div className="text-gray-400 text-sm truncate">{email.subject || '(Tanpa subjek)'}</div>
                      <div className="text-gray-600 text-xs mt-1">{new Date(email.received_at).toLocaleString('id-ID', { hour: '2-digit', minute: '2-digit', day: 'numeric', month: 'short' })}</div>
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}

        {/* Email Aktif List */}
        {!activeAlias && aliases.length > 0 && (
          <div>
            <h2 className="text-lg font-semibold text-gray-300 mb-3">Email Aktif</h2>
            <div className="space-y-3">
              {aliases.map((alias: Alias) => (
                <div key={alias.id} onClick={() => { setActiveAlias(alias); loadEmails(alias.email); }} className="card card-hover cursor-pointer p-4">
                  <div className="flex items-center gap-3">
                    <div className="w-9 h-9 bg-gradient-to-br from-purple-500 to-blue-500 rounded-lg flex items-center justify-center flex-shrink-0">
                      <svg className="w-4 h-4 text-white" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M3 8l7.89 5.26a2 2 0 002.22 0L21 8M5 19h14a2 2 0 002-2V7a2 2 0 00-2-2H5a2 2 0 00-2 2v10a2 2 0 002 2z" /></svg>
                    </div>
                    <div className="flex-1 min-w-0">
                      <p className="font-mono font-semibold text-gray-200 text-sm break-all">{alias.email}</p>
                      <div className="flex items-center gap-2 mt-1">
                        <span className="text-xs px-2 py-0.5 rounded-full bg-blue-500/10 text-blue-400 border border-blue-500/20">{alias.email_count} email</span>
                        <span className="text-xs text-gray-500">Exp: {new Date(alias.expires_at).toLocaleString('id-ID', { hour: '2-digit', minute: '2-digit', day: 'numeric', month: 'short' })}</span>
                      </div>
                    </div>
                    <div className="flex items-center gap-2">
                      <button onClick={(e) => { e.stopPropagation(); navigator.clipboard.writeText(alias.email); setShowToast(true); setTimeout(() => setShowToast(false), 2000); }} className="p-2 text-gray-400 hover:text-white rounded-lg transition-all" title="Copy">
                        <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M8 16H6a2 2 0 01-2-2V6a2 2 0 012-2h8a2 2 0 012 2v2m-6 12h8a2 2 0 002-2v-8a2 2 0 00-2-2h-8a2 2 0 00-2 2v8a2 2 0 002 2z" /></svg>
                      </button>
                      <button onClick={(e) => { e.stopPropagation(); handleDeleteAlias(alias.email); }} className="p-2 text-red-400 hover:text-red-300 rounded-lg transition-all" title="Hapus">
                        <svg className="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" /></svg>
                      </button>
                      <svg className="w-5 h-5 text-gray-600" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M9 5l7 7-7 7" /></svg>
                    </div>
                  </div>
                </div>
              ))}
            </div>
          </div>
        )}
      </div>

      {showSendModal && <SendEmailModal aliases={aliases} onClose={() => setShowSendModal(false)} onSuccess={() => { setShowToast(true); setTimeout(() => setShowToast(false), 2000); }} />}
      <CopyToast show={showToast} />
    </main>
  );
}
