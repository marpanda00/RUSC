"use client";

import { useEffect, useState } from "react";

const FLOORS = [
  { floor: 1, label: "First floor", count: "1 vibration" },
  { floor: 2, label: "Second floor", count: "2 vibrations" },
  { floor: 3, label: "Third floor", count: "3 vibrations" },
];

export default function Page() {
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState("Tap the floor where you left MarioPhone.");

  async function refresh() {
    const response = await fetch("/api/status", { cache: "no-store" });
    const data = await response.json();
    if (!response.ok) {
      setStatus(data.error || "Could not load status");
      return;
    }
    if (data.last_message) {
      setStatus(
        `${data.last_message}\n${data.pending_vibrations ? `${data.pending_vibrations} vibration(s) queued for the watch.` : "No buzz waiting."}`,
      );
    }
  }

  async function missed(floor) {
    setBusy(true);
    try {
      const response = await fetch("/api/missed", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({ floor }),
      });
      const data = await response.json();
      if (!response.ok) {
        setStatus(data.error || "Request failed");
        return;
      }
      const alert = data.alert;
      setStatus(
        `${alert.message}\nThe watch should vibrate ${alert.vibrations} time${alert.vibrations === 1 ? "" : "s"}.`,
      );
    } catch (error) {
      setStatus(String(error));
    } finally {
      setBusy(false);
    }
  }

  useEffect(() => {
    refresh();
  }, []);

  return (
    <main>
      <h1>Missed MarioPhone</h1>
      <p className="lede">
        Phone out of reach of the watch? Tap the floor you left it on. First floor
        is one buzz, second is two, third is three.
      </p>
      {FLOORS.map((item) => (
        <button
          key={item.floor}
          className={item.floor === 2 ? "primary" : undefined}
          disabled={busy}
          onClick={() => missed(item.floor)}
        >
          {item.label}
          <span className="count">{item.count}</span>
        </button>
      ))}
      <div className="status">{status}</div>
    </main>
  );
}
