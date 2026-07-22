import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'RoutersSH TempMail - Email Sementara',
  description: 'Email sementara untuk melindungi privasi Anda. Generate email random, terima email langsung, tanpa registrasi.',
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="id" className="dark">
      <body className="min-h-screen">
        {children}
      </body>
    </html>
  );
}
