#!/usr/bin/env python3
"""Backtest v2: one row per company, compare 1-month vs 2-month holds."""

from __future__ import annotations

import argparse
import html
import json
import re
from pathlib import Path

import numpy as np
import pandas as pd
import yfinance as yf
from scipy import stats

from ticker_map import TICKER_MAP

DATA_PATH = Path(__file__).parent / "company_data.tsv"
OUTPUT_DIR = Path(__file__).parent / "output_v2"
TODAY = pd.Timestamp("2026-09-07")

# Skip rows where company name is clearly corrupted (parsing artifacts)
GARBAGE_PATTERNS = [
    r"^\d+ ",
    r"billion",
    r"trillion",
    r"APPROACH study",
    r"ALKIVIA results",
    r"AMPLIFY-7P",
    r"IDH1-mutant",
    r"diabetes\.",
    r"years and older",
    r"paediatric insomnia",
    r"IPF data for",
    r"plozasiran data",
    r"ABCL635",
    r"million and CARVYKTI",
    r"from \$4 whilst",
    r"and SHASTA-4",
    r"trial of LB-102",
    r"Emerge trial",
    r"BOND-003",
    r"study of gedatolisib",
    r"tumor types using",
]


def parse_eval(value: str | float) -> float | None:
    if pd.isna(value) or value == "":
        return None
    s = html.unescape(str(value).strip()).replace("%", "")
    try:
        return float(s)
    except ValueError:
        return None


def clean_company(name: str) -> str | None:
    if pd.isna(name) or not str(name).strip():
        return None
    name = html.unescape(str(name).strip())
    for pat in GARBAGE_PATTERNS:
        if re.search(pat, name, re.I):
            return None
    if len(name) < 3 or name[0].isdigit():
        return None
    return name


def load_positions(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path, sep="\t", dtype=str)
    df.columns = [c.strip().lower() for c in df.columns]
    rename = {"evaluation": "eval_pct"}
    df = df.rename(columns=rename)
    df["email_date"] = pd.to_datetime(df["email_date"], format="mixed")
    df["rank"] = pd.to_numeric(df["rank"], errors="coerce").astype("Int64")
    df["eval_pct"] = df["eval_pct"].map(parse_eval)
    df["company"] = df["company"].map(clean_company)
    df = df.dropna(subset=["company", "eval_pct", "rank"])
    df["company"] = df["company"].astype(str)
    df["ticker"] = df["company"].map(TICKER_MAP)
    return df.reset_index(drop=True)


def nearest_price_on_or_after(series: pd.Series, target: pd.Timestamp):
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


_PRICE_CACHE: dict[tuple[str, str, str], pd.Series] = {}


def fetch_trade(ticker: str, buy_date: pd.Timestamp, sell_target: pd.Timestamp) -> dict:
    start = buy_date - pd.Timedelta(days=5)
    end = min(sell_target + pd.Timedelta(days=10), TODAY + pd.Timedelta(days=1))
    cache_key = (ticker, str(start.date()), str(end.date()))
    try:
        if cache_key in _PRICE_CACHE:
            hist = _PRICE_CACHE[cache_key].to_frame("Close")
        else:
            hist = yf.Ticker(ticker).history(start=start.date(), end=end.date(), auto_adjust=True)
            if not hist.empty and "Close" in hist.columns:
                _PRICE_CACHE[cache_key] = hist["Close"]
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
        return {"status": "incomplete", "buy_date": buy_dt, "buy_price": buy_price}
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


def run_hold_period(positions: pd.DataFrame, months: int) -> pd.DataFrame:
    pos = positions.copy()
    pos["hold_months"] = months
    pos["sell_target_date"] = pos["email_date"] + pd.DateOffset(months=months)
    results = []
    for _, row in pos.iterrows():
        ticker = row["ticker"]
        if not ticker:
            results.append({**row.to_dict(), "status": "no_ticker"})
            continue
        trade = fetch_trade(ticker, row["email_date"], row["sell_target_date"])
        results.append({**row.to_dict(), **trade})
        if len(results) % 200 == 0:
            print(f"  {months}M: processed {len(results)}/{len(pos)}")
    return pd.DataFrame(results)


def portfolio_stats(ok: pd.DataFrame) -> dict:
    if len(ok) == 0:
        return {}
    return {
        "positions": len(ok),
        "invested_usd": len(ok),
        "end_value_usd": round(ok["end_value_usd"].sum(), 2),
        "total_return_pct": round((ok["end_value_usd"].sum() / len(ok) - 1) * 100, 2),
        "avg_return_pct": round(ok["return_pct"].mean(), 2),
        "median_return_pct": round(ok["return_pct"].median(), 2),
        "win_rate_pct": round((ok["return_pct"] > 0).mean() * 100, 1),
    }


def eval_correlation(ok: pd.DataFrame) -> dict:
    r, p = stats.pearsonr(ok["eval_pct"], ok["return_pct"])
    sr, sp = stats.spearmanr(ok["eval_pct"], ok["return_pct"])
    return {
        "pearson_r": round(r, 4),
        "pearson_p": round(p, 4),
        "spearman_r": round(sr, 4),
        "spearman_p": round(sp, 4),
    }


def by_rank(ok: pd.DataFrame) -> dict:
    g = (
        ok.groupby("rank")
        .agg(
            count=("return_pct", "count"),
            avg_return_pct=("return_pct", "mean"),
            avg_eval_pct=("eval_pct", "mean"),
            win_rate_pct=("return_pct", lambda s: (s > 0).mean() * 100),
        )
        .round(2)
    )
    return g.to_dict(orient="index")


def by_eval_bucket(ok: pd.DataFrame) -> dict:
    bins = [0, 5, 10, 20, 30, 50, 100, 1000]
    labels = ["0-5%", "5-10%", "10-20%", "20-30%", "30-50%", "50-100%", "100%+"]
    ok = ok.copy()
    ok["eval_bucket"] = pd.cut(ok["eval_pct"], bins=bins, labels=labels, right=True)
    g = (
        ok.groupby("eval_bucket", observed=True)
        .agg(
            count=("return_pct", "count"),
            avg_return_pct=("return_pct", "mean"),
            win_rate_pct=("return_pct", lambda s: (s > 0).mean() * 100),
        )
        .round(2)
    )
    return g.to_dict(orient="index")


def rank_band_stats(ok: pd.DataFrame) -> dict:
    bands = {
        "rank_1_top_mover": ok[ok["rank"] == 1],
        "rank_2_3": ok[ok["rank"].isin([2, 3])],
        "rank_4_7_mid": ok[ok["rank"].between(4, 7)],
        "rank_8_10_bottom": ok[ok["rank"].between(8, 10)],
    }
    out = {}
    for name, sub in bands.items():
        if len(sub):
            out[name] = {
                "count": len(sub),
                "avg_return_pct": round(sub["return_pct"].mean(), 2),
                "win_rate_pct": round((sub["return_pct"] > 0).mean() * 100, 1),
                "avg_eval_pct": round(sub["eval_pct"].mean(), 2),
            }
    return out


def strategy_stats(ok: pd.DataFrame) -> dict:
    strategies = {}

    # Only bottom ranks (8-10) per email - one pick each? or all bottom 3
    bottom = ok[ok["rank"] >= 8]
    strategies["all_rank_8_to_10"] = portfolio_stats(bottom)

    top = ok[ok["rank"] == 1]
    strategies["rank_1_only"] = portfolio_stats(top)

    low_eval = ok[ok["eval_pct"] < 10]
    strategies["eval_under_10pct"] = portfolio_stats(low_eval)

    high_eval = ok[ok["eval_pct"] >= 30]
    strategies["eval_30plus"] = portfolio_stats(high_eval)

    # Per email: buy only lowest-ranked company (rank 10 or max rank)
    per_email_bottom = []
    for (_, _), g in ok.groupby(["email_date", "email_subject"]):
        per_email_bottom.append(g.loc[g["rank"].idxmax()])
    if per_email_bottom:
        bdf = pd.DataFrame(per_email_bottom)
        strategies["lowest_rank_per_email"] = portfolio_stats(bdf)

    per_email_top = []
    for (_, _), g in ok.groupby(["email_date", "email_subject"]):
        per_email_top.append(g.loc[g["rank"].idxmin()])
    if per_email_top:
        tdf = pd.DataFrame(per_email_top)
        strategies["highest_rank_per_email"] = portfolio_stats(tdf)

    return strategies


def build_summary(df: pd.DataFrame, months: int) -> dict:
    ok = df[df["status"] == "ok"].copy()
    ok["eval_pct"] = ok["eval_pct"].astype(float)
    ok["return_pct"] = ok["return_pct"].astype(float)
    ok["rank"] = ok["rank"].astype(int)

    summary = {
        "hold_months": months,
        "total_positions": len(df),
        "unique_companies": df["company"].nunique(),
        "completed_trades": len(ok),
        "incomplete_trades": int((df["status"] == "incomplete").sum()),
        "missing_ticker": int((df["status"] == "no_ticker").sum()),
        "failed_lookup": int(~df["status"].isin(["ok", "incomplete", "no_ticker"]).sum()),
        "portfolio_all": portfolio_stats(ok),
        "eval_correlation": eval_correlation(ok) if len(ok) > 10 else {},
        "by_rank": by_rank(ok),
        "by_eval_bucket": by_eval_bucket(ok),
        "rank_bands": rank_band_stats(ok),
        "strategies": strategy_stats(ok),
    }
    return summary


def compare_holds(s1: dict, s2: dict) -> dict:
    p1 = s1.get("portfolio_all", {})
    p2 = s2.get("portfolio_all", {})
    return {
        "1_month_total_return_pct": p1.get("total_return_pct"),
        "2_month_total_return_pct": p2.get("total_return_pct"),
        "better_hold_period": "2_month" if (p2.get("total_return_pct") or -999) > (p1.get("total_return_pct") or -999) else "1_month",
        "1_month_win_rate": p1.get("win_rate_pct"),
        "2_month_win_rate": p2.get("win_rate_pct"),
        "rank_bands_1m": s1.get("rank_bands", {}),
        "rank_bands_2m": s2.get("rank_bands", {}),
        "strategies_1m": s1.get("strategies", {}),
        "strategies_2m": s2.get("strategies", {}),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default=str(DATA_PATH))
    args = parser.parse_args()

    OUTPUT_DIR.mkdir(exist_ok=True)
    positions = load_positions(Path(args.data))
    print(f"Loaded {len(positions)} positions ({positions['company'].nunique()} companies)")
    print(f"Ticker coverage: {positions['ticker'].notna().sum()}/{len(positions)}")

    print("\nRunning 1-month hold backtest...")
    r1 = run_hold_period(positions, 1)
    s1 = build_summary(r1, 1)
    r1.to_csv(OUTPUT_DIR / "results_1month.csv", index=False)

    print("Running 2-month hold backtest...")
    r2 = run_hold_period(positions, 2)
    s2 = build_summary(r2, 2)
    r2.to_csv(OUTPUT_DIR / "results_2month.csv", index=False)

    comparison = compare_holds(s1, s2)
    full = {"1_month": s1, "2_month": s2, "comparison": comparison}
    with open(OUTPUT_DIR / "summary_v2.json", "w") as f:
        json.dump(full, f, indent=2, default=str)

    print("\n" + "=" * 70)
    print("PHARMA BACKTEST V2 — 1 MONTH vs 2 MONTH")
    print("=" * 70)
    print(json.dumps(full, indent=2, default=str))

    missing = sorted(positions.loc[positions["ticker"].isna(), "company"].unique())
    if missing:
        with open(OUTPUT_DIR / "missing_tickers.txt", "w") as f:
            f.write("\n".join(missing))
        print(f"\nMissing tickers ({len(missing)}): see {OUTPUT_DIR / 'missing_tickers.txt'}")


if __name__ == "__main__":
    main()
