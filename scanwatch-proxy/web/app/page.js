"use client";

import { useEffect, useState } from "react";

const FLOORS = [
  { floor: 1, label: "First floor", count: "1 vibration when a proxy sees the watch" },
  { floor: 2, label: "Second floor", count: "2 vibrations when a proxy sees the watch" },
  { floor: 3, label: "Third floor", count: "3 vibrations when a proxy sees the watch" },
];

export default function Page() {
  const [status, setStatus] = useState("Waiting for a house proxy to see MarioPhone…");
  const [floor, setFloor] = useState(null);

  async function refresh() {
    const response = await fetch("/api/status", { cache: "no-store" });
    const data = await response.json();
    if (!response.ok) {
      setStatus(data.error || "Could not load status");
      return;
    }
    setFloor(data.floor);
    if (!data.floor) {
      setStatus(
        "No floor yet. A house proxy records it when MarioPhone is visible on that floor. Taps do not buzz the watch.",
      );
      return;
    }
    const extra = data.last_error ? `\nLast proxy error: ${data.last_error}` : "";
    setStatus(
      `${data.last_message || `MarioPhone last seen on floor ${data.floor}`}\nWhen a proxy sees the ScanWatch it will vibrate ${data.floor} time${data.floor === 1 ? "" : "s"}.${extra}`,
    );
  }

  useEffect(() => {
    refresh();
    const id = setInterval(refresh, 4000);
    return () => clearInterval(id);
  }, []);

  return (
    <main>
      <h1>Missed MarioPhone</h1>
      <p className="lede">
        The floor comes from which house proxy can see MarioPhone. The ScanWatch
        vibrates by itself when a proxy sees the watch — never from a tap.
      </p>
      {FLOORS.map((item) => (
        <div
          key={item.floor}
          className={item.floor === floor ? "floor-card current" : "floor-card"}
        >
          {item.label}
          <span className="count">{item.count}</span>
        </div>
      ))}
      <div className="status">{status}</div>
    </main>
  );
}