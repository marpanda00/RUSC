#!/usr/bin/env python3
"""Score Daily Market Movers picks: BUY / MAYBE / SKIP."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

import pandas as pd

from backtest_v2 import DATA_PATH, PRIMARY_SIGNAL, load_positions, score_pick

DEFAULT_SUBJECT = "Daily Market Movers: Global Majors & Industry"


def parse_pick_line(line: str) -> tuple[int, str, float] | None:
    """Parse 'rank|company|eval%' or 'rank,company,eval'."""
    line = line.strip()
    if not line or line.startswith("#"):
        return None
    parts = re.split(r"[|\t,]", line)
    if len(parts) < 3:
        return None
    rank = int(parts[0].strip())
    company = parts[1].strip()
    eval_s = parts[2].strip().replace("%", "")
    return rank, company, float(eval_s)


def score_lines(lines: list[str], email_subject: str) -> list[dict]:
    results = []
    for line in lines:
        parsed = parse_pick_line(line)
        if parsed is None:
            continue
        rank, company, eval_pct = parsed
        results.append(score_pick(company, rank, eval_pct, email_subject))
    return results


def print_results(results: list[dict]) -> None:
    if not results:
        print("No picks to score.")
        return
    print(f"\nDaily checklist (primary signal: {PRIMARY_SIGNAL})")
    print("=" * 72)
    buys = [r for r in results if r["verdict"] == "BUY"]
    maybes = [r for r in results if r["verdict"] == "MAYBE"]
    skips = [r for r in results if r["verdict"] == "SKIP"]

    for group, label in [(buys, "BUY"), (maybes, "MAYBE"), (skips, "SKIP")]:
        if not group:
            continue
        print(f"\n{label}:")
        for r in group:
            ticker = r.get("ticker") or "?"
            print(
                f"  [{r['verdict']}] rank {r['rank']:>2}  {r['eval_pct']:>5.1f}%  "
                f"{r['company']:<28} ({ticker})"
            )
            print(f"         {r['reason']}")
            if r.get("matched_signals"):
                print(f"         signals: {', '.join(r['matched_signals'])}")

    print(f"\nSummary: {len(buys)} BUY, {len(maybes)} MAYBE, {len(skips)} SKIP")


def load_date_from_tsv(path: Path, date: str) -> tuple[list[str], str]:
    df = load_positions(path)
    target = pd.to_datetime(date).normalize()
    day = df[df["email_date"].dt.normalize() == target]
    if day.empty:
        raise SystemExit(f"No rows for {date} in {path}")
    subject = day.iloc[0]["email_subject"]
    lines = [
        f"{int(row['rank'])}|{row['company']}|{row['eval_pct']}"
        for _, row in day.iterrows()
    ]
    return lines, subject


def main():
    parser = argparse.ArgumentParser(description="Score Market Movers picks (BUY/SKIP)")
    parser.add_argument(
        "picks",
        nargs="*",
        help='Pick lines: "rank|company|eval%" e.g. "9|Scholar Rock|4%"',
    )
    parser.add_argument("--date", help="Load all picks for YYYY-MM-DD from company_data.tsv")
    parser.add_argument("--data", default=str(DATA_PATH))
    parser.add_argument("--daily", action="store_true", help="Email is Daily (default subject)")
    parser.add_argument("--weekly", action="store_true", help="Email is Weekly")
    parser.add_argument("--subject", help="Full email subject line")
    args = parser.parse_args()

    if args.date:
        lines, subject = load_date_from_tsv(Path(args.data), args.date)
    else:
        subject = args.subject or DEFAULT_SUBJECT
        if args.weekly:
            subject = subject.replace("Daily", "Weekly") if "Daily" in subject else f"Weekly {subject}"
        lines = list(args.picks)
        if not lines and not sys.stdin.isatty():
            lines = sys.stdin.read().splitlines()

    if not lines:
        parser.print_help()
        print("\nExamples:")
        print('  python score_picks.py "9|Scholar Rock|4%" "10|Agios|3%"')
        print("  python score_picks.py --date 2026-08-18")
        print('  echo "8|Kyverna|11%" | python score_picks.py')
        raise SystemExit(1)

    results = score_lines(lines, subject)
    print_results(results)


if __name__ == "__main__":
    main()
