import "./globals.css";

export const metadata = {
  title: "Missed MarioPhone",
  description: "Buzz the ScanWatch once per floor when the phone is out of reach",
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
