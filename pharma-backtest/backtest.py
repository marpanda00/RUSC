#!/usr/bin/env python3
"""Backtest: $1 invested in each quoted company at email date, sold ~1 month later."""

from __future__ import annotations

import json
import re
from datetime import timedelta
from pathlib import Path

import numpy as np
import pandas as pd
import yfinance as yf
from scipy import stats

from ticker_map import TICKER_MAP

DATA_PATH = Path(__file__).parent / "email_data.csv"
OUTPUT_DIR = Path(__file__).parent / "output"
TODAY = pd.Timestamp("2026-09-06")


def parse_eval(value: str | float) -> float | None:
    if pd.isna(value) or value == "":
        return None
    s = str(value).strip().replace("%", "")
    try:
        return float(s)
    except ValueError:
        return None


def load_positions() -> pd.DataFrame:
    df = pd.read_csv(DATA_PATH)
    df["email_date"] = pd.to_datetime(df["email_date"], format="mixed")

    rows = []
    for _, row in df.iterrows():
        for rank in (1, 2, 3):
            company = row.get(f"company_{rank}")
            eval_val = parse_eval(row.get(f"eval_{rank}"))
            if pd.isna(company) or not str(company).strip():
                continue
            company = str(company).strip()
            rows.append(
                {
                    "email_date": row["email_date"],
                    "email_subject": row["email_subject"],
                    "company": company,
                    "eval_pct": eval_val,
                    "rank": rank,
                    "notes": row.get("notes", ""),
                }
            )

    positions = pd.DataFrame(rows)
    positions["ticker"] = positions["company"].map(TICKER_MAP)
    positions["sell_target_date"] = positions["email_date"] + pd.DateOffset(months=1)
    return positions


def nearest_price_on_or_after(series: pd.Series, target: pd.Timestamp) -> tuple[pd.Timestamp, float] | None:
    idx = series.index
    if idx.tz is not None:
        target = target.tz_localize(idx.tz) if target.tzinfo is None else target.tz_convert(idx.tz)
    valid = idx[idx >= target]
    if len(valid) == 0:
        return None
    date = valid[0]
    price = float(series.loc[date])
    if np.isnan(price):
        return None
    return date, price


def fetch_trade(
    ticker: str,
    buy_date: pd.Timestamp,
    sell_target: pd.Timestamp,
) -> dict:
    start = buy_date - pd.Timedelta(days=5)
    end = min(sell_target + pd.Timedelta(days=10), TODAY + pd.Timedelta(days=1))

    try:
        hist = yf.Ticker(ticker).history(start=start.date(), end=end.date(), auto_adjust=True)
    except Exception as exc:
        return {"status": "error", "error": str(exc)}

    if hist.empty or "Close" not in hist.columns:
        return {"status": "no_data"}

    close = hist["Close"]
    buy = nearest_price_on_or_after(close, buy_date)
    if buy is None:
        return {"status": "no_buy_price"}

    buy_dt, buy_price = buy
    sell = nearest_price_on_or_after(close, sell_target)
    if sell is None:
        return {
            "status": "incomplete",
            "buy_date": buy_dt,
            "buy_price": buy_price,
            "sell_target_date": sell_target,
        }

    sell_dt, sell_price = sell
    ret = (sell_price - buy_price) / buy_price
    return {
        "status": "ok",
        "buy_date": buy_dt,
        "buy_price": buy_price,
        "sell_date": sell_dt,
        "sell_price": sell_price,
        "return_pct": ret * 100,
        "end_value_usd": 1.0 * (1 + ret),
        "holding_days": (sell_dt - buy_dt).days,
    }


def run_backtest() -> tuple[pd.DataFrame, dict]:
    positions = load_positions()
    unique_tickers = sorted(positions["ticker"].dropna().unique())

    # Batch download for speed
    min_date = positions["email_date"].min() - pd.Timedelta(days=5)
    max_date = min(positions["sell_target_date"].max() + pd.Timedelta(days=10), TODAY + pd.Timedelta(days=1))

    print(f"Downloading prices for {len(unique_tickers)} tickers...")
    try:
        bulk = yf.download(
            unique_tickers,
            start=min_date.date(),
            end=max_date.date(),
            auto_adjust=True,
            progress=False,
            group_by="ticker",
            threads=True,
        )
    except Exception:
        bulk = None

    results = []
    for i, row in positions.iterrows():
        ticker = row["ticker"]
        if not ticker:
            results.append({**row.to_dict(), "status": "no_ticker"})
            continue

        trade = fetch_trade(ticker, row["email_date"], row["sell_target_date"])
        results.append({**row.to_dict(), **trade})

        if (len(results) % 50) == 0:
            print(f"  processed {len(results)}/{len(positions)} positions")

    out = pd.DataFrame(results)
    summary = build_summary(out)
    return out, summary


def build_summary(df: pd.DataFrame) -> dict:
    ok = df[df["status"] == "ok"].copy()
    incomplete = df[df["status"] == "incomplete"]
    no_ticker = df[df["status"] == "no_ticker"]
    failed = df[~df["status"].isin(["ok", "incomplete", "no_ticker"])]

    summary = {
        "total_positions": len(df),
        "unique_companies": df["company"].nunique(),
        "completed_trades": len(ok),
        "incomplete_trades": len(incomplete),
        "missing_ticker_mapping": len(no_ticker),
        "failed_price_lookup": len(failed),
    }

    if len(ok) == 0:
        return summary

    ok["eval_pct"] = ok["eval_pct"].astype(float)
    ok["return_pct"] = ok["return_pct"].astype(float)

    # Portfolio: equal $1 per position
    total_invested = len(ok)
    total_end_value = ok["end_value_usd"].sum()
    summary["portfolio"] = {
        "invested_usd": total_invested,
        "end_value_usd": round(total_end_value, 2),
        "total_return_pct": round((total_end_value / total_invested - 1) * 100, 2),
        "avg_return_per_position_pct": round(ok["return_pct"].mean(), 2),
        "median_return_pct": round(ok["return_pct"].median(), 2),
        "win_rate_pct": round((ok["return_pct"] > 0).mean() * 100, 1),
        "best_trade_pct": round(ok["return_pct"].max(), 2),
        "worst_trade_pct": round(ok["return_pct"].min(), 2),
    }

    # Eval relevance
    pearson_r, pearson_p = stats.pearsonr(ok["eval_pct"], ok["return_pct"])
    spearman_r, spearman_p = stats.spearmanr(ok["eval_pct"], ok["return_pct"])

    summary["eval_relevance"] = {
        "pearson_correlation": round(pearson_r, 4),
        "pearson_p_value": round(pearson_p, 4),
        "spearman_correlation": round(spearman_r, 4),
        "spearman_p_value": round(spearman_p, 4),
        "interpretation": interpret_correlation(pearson_r, pearson_p),
    }

    # By eval bucket
    bins = [0, 10, 20, 30, 50, 100, 1000]
    labels = ["0-10%", "10-20%", "20-30%", "30-50%", "50-100%", "100%+"]
    ok["eval_bucket"] = pd.cut(ok["eval_pct"], bins=bins, labels=labels, right=True)
    bucket = (
        ok.groupby("eval_bucket", observed=True)
        .agg(
            count=("return_pct", "count"),
            avg_return_pct=("return_pct", "mean"),
            median_return_pct=("return_pct", "median"),
            win_rate_pct=("return_pct", lambda s: (s > 0).mean() * 100),
            avg_eval_pct=("eval_pct", "mean"),
        )
        .round(2)
    )
    summary["by_eval_bucket"] = bucket.to_dict(orient="index")

    # By rank (company_1 vs company_2 vs company_3)
    by_rank = (
        ok.groupby("rank")
        .agg(
            count=("return_pct", "count"),
            avg_return_pct=("return_pct", "mean"),
            avg_eval_pct=("eval_pct", "mean"),
            win_rate_pct=("return_pct", lambda s: (s > 0).mean() * 100),
        )
        .round(2)
    )
    summary["by_rank"] = by_rank.to_dict(orient="index")

    # Strategy: only invest in top eval per email
    top_per_email = ok.loc[ok.groupby(["email_date", "email_subject"])["eval_pct"].idxmax()]
    summary["top_eval_only_strategy"] = {
        "positions": len(top_per_email),
        "avg_return_pct": round(top_per_email["return_pct"].mean(), 2),
        "total_return_pct": round((top_per_email["end_value_usd"].sum() / len(top_per_email) - 1) * 100, 2),
        "win_rate_pct": round((top_per_email["return_pct"] > 0).mean() * 100, 1),
    }

    # Strategy: only invest eval >= 30%
    high_eval = ok[ok["eval_pct"] >= 30]
    summary["high_eval_30plus_strategy"] = {
        "positions": len(high_eval),
        "avg_return_pct": round(high_eval["return_pct"].mean(), 2) if len(high_eval) else None,
        "win_rate_pct": round((high_eval["return_pct"] > 0).mean() * 100, 1) if len(high_eval) else None,
    }

    low_eval = ok[ok["eval_pct"] < 30]
    summary["low_eval_under30_strategy"] = {
        "positions": len(low_eval),
        "avg_return_pct": round(low_eval["return_pct"].mean(), 2) if len(low_eval) else None,
        "win_rate_pct": round((low_eval["return_pct"] > 0).mean() * 100, 1) if len(low_eval) else None,
    }

    return summary


def interpret_correlation(r: float, p: float) -> str:
    if p > 0.05:
        strength = "no statistically significant"
    elif abs(r) < 0.1:
        strength = "negligible"
    elif abs(r) < 0.3:
        strength = "weak"
    elif abs(r) < 0.5:
        strength = "moderate"
    else:
        strength = "strong"

    direction = "positive" if r > 0 else "negative"
    sig = " (not significant at 5%)" if p > 0.05 else " (statistically significant at 5%)"
    return f"{strength} {direction} relationship between eval score and 1-month return{sig}"


def main():
    OUTPUT_DIR.mkdir(exist_ok=True)
    results, summary = run_backtest()

    results.to_csv(OUTPUT_DIR / "backtest_results.csv", index=False)
    with open(OUTPUT_DIR / "summary.json", "w") as f:
        json.dump(summary, f, indent=2, default=str)

    print("\n" + "=" * 60)
    print("PHARMA MARKET MOVERS — 1-MONTH BACKTEST")
    print("=" * 60)
    print(json.dumps(summary, indent=2, default=str))

    ok = results[results["status"] == "ok"]
    if len(ok):
        print("\n--- Top 10 best trades ---")
        cols = ["email_date", "company", "eval_pct", "return_pct", "ticker"]
        print(ok.nlargest(10, "return_pct")[cols].to_string(index=False))
        print("\n--- Top 10 worst trades ---")
        print(ok.nsmallest(10, "return_pct")[cols].to_string(index=False))

    missing = results[results["status"] == "no_ticker"]["company"].unique()
    if len(missing):
        print(f"\n--- Companies without ticker mapping ({len(missing)}) ---")
        for c in sorted(missing):
            print(f"  - {c}")

    print(f"\nFull results: {OUTPUT_DIR / 'backtest_results.csv'}")


if __name__ == "__main__":
    main()
