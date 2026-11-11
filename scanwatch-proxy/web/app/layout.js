import "./globals.css";

export const metadata = {
  title: "Missed MarioPhone",
  description: "Buzz the ScanWatch from house proxies using MarioPhone visibility",
};

export const viewport = {
  width: "device-width",
  initialScale: 1,
};

export default function RootLayout({ children }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
