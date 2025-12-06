import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "SimpleOS",
  description: "A Unix-like operating system running in your browser",
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="en">
      <body className="bg-black overflow-hidden">{children}</body>
    </html>
  );
}
