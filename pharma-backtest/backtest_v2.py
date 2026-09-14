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

# Non-US exchange suffixes on Yahoo Finance tickers
FOREIGN_TICKER_PATTERN = re.compile(
    r"\.(T|HK|KS|KQ|TW|SS|SZ|PA|DE|SW|L|AX|NS|BO|MI|CO|ST|TO|IS|IR|HE|OL|MX|C|LJ|SI|MC)$",
    re.I,
)

# Named focused signal filters (applied after enrich_positions)
PRIMARY_SIGNAL = "us_daily_rank_8_10_eval_lte_8"

FOCUSED_SIGNALS: dict[str, dict] = {
    "us_rank_8_10": {
        "label": "US ranks 8-10 (baseline)",
        "description": "US-listed, rank 8-10",
    },
    "us_rank_10_eval_lte_8": {
        "label": "Rank 10 + eval ≤ 8%",
        "description": "US-listed, rank 10, evaluation ≤ 8%",
        "rank": 10,
        "eval_max": 8,
    },
    "us_daily_rank_10_eval_lte_7": {
        "label": "Daily + rank 10 + eval ≤ 7%",
        "description": "US Daily email, rank 10, evaluation ≤ 7%",
        "daily": True,
        "rank": 10,
        "eval_max": 7,
    },
    "us_daily_eval_4_8": {
        "label": "Daily + eval 4-8% (ranks 8-10)",
        "description": "US Daily email, ranks 8-10, evaluation 4-8%",
        "daily": True,
        "rank_min": 8,
        "rank_max": 10,
        "eval_min": 4,
        "eval_max": 8,
    },
    "us_daily_rank_8_10_eval_lte_8": {
        "label": "Daily + eval ≤ 8% (ranks 8-10)",
        "description": "US Daily email, ranks 8-10, evaluation ≤ 8% (recommended)",
        "daily": True,
        "rank_min": 8,
        "rank_max": 10,
        "eval_max": 8,
    },
}

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
    return enrich_positions(df.reset_index(drop=True))


def is_us_ticker(ticker: str | None) -> bool:
    if not ticker or pd.isna(ticker):
        return False
    return not bool(FOREIGN_TICKER_PATTERN.search(str(ticker)))


def enrich_positions(df: pd.DataFrame) -> pd.DataFrame:
    """Add columns used by focused signal filters."""
    df = df.copy()
    df["is_us"] = df["ticker"].map(is_us_ticker)
    df["is_daily"] = df["email_subject"].str.contains("Daily", case=False, na=False)
    df["is_weekly"] = df["email_subject"].str.contains("Weekly", case=False, na=False)
    df["is_asia"] = df["email_subject"].str.contains("Asia", case=False, na=False)
    return df


def pick_matches_signal(row: pd.Series, signal: str) -> bool:
    """True if a single enriched row matches a focused signal."""
    if signal not in FOCUSED_SIGNALS:
        raise ValueError(f"Unknown signal '{signal}'. Choose from: {list(FOCUSED_SIGNALS)}")
    return len(apply_focused_filter(pd.DataFrame([row]), signal)) == 1


def score_pick(
    company: str,
    rank: int,
    eval_pct: float,
    email_subject: str = "Daily Market Movers: Global Majors & Industry",
    ticker: str | None = None,
) -> dict:
    """Score one email row: BUY/SKIP vs focused signals."""
    cleaned = clean_company(company)
    if not cleaned:
        return {"verdict": "SKIP", "reason": "Invalid or corrupted company name", "company": company}

    ticker = ticker or TICKER_MAP.get(cleaned)
    row = enrich_positions(
        pd.DataFrame(
            [
                {
                    "company": cleaned,
                    "rank": rank,
                    "eval_pct": eval_pct,
                    "email_subject": email_subject,
                    "ticker": ticker,
                    "email_date": pd.Timestamp.today(),
                }
            ]
        )
    ).iloc[0]

    reasons: list[str] = []
    if not row["is_us"]:
        reasons.append("not US-listed (or unknown ticker)")
    if not row["is_daily"]:
        reasons.append("not Daily email")
    if not (8 <= rank <= 10):
        reasons.append(f"rank {rank} outside 8–10")
    if eval_pct > 8:
        reasons.append(f"eval {eval_pct}% > 8%")

    matched = [key for key in FOCUSED_SIGNALS if pick_matches_signal(row, key)]
    primary = PRIMARY_SIGNAL in matched

    if primary:
        verdict = "BUY"
        reason = FOCUSED_SIGNALS[PRIMARY_SIGNAL]["description"]
    elif matched:
        verdict = "MAYBE"
        reason = f"Matches {', '.join(matched)} but not primary ({PRIMARY_SIGNAL})"
    else:
        verdict = "SKIP"
        reason = "; ".join(reasons) if reasons else "No focused signal match"

    return {
        "verdict": verdict,
        "reason": reason,
        "company": cleaned,
        "ticker": ticker,
        "rank": rank,
        "eval_pct": eval_pct,
        "is_us": bool(row["is_us"]),
        "is_daily": bool(row["is_daily"]),
        "matched_signals": matched,
        "primary_signal": PRIMARY_SIGNAL,
    }


def apply_focused_filter(df: pd.DataFrame, signal: str) -> pd.DataFrame:
    """Return rows matching a named focused signal."""
    if signal not in FOCUSED_SIGNALS:
        raise ValueError(f"Unknown signal '{signal}'. Choose from: {list(FOCUSED_SIGNALS)}")

    spec = FOCUSED_SIGNALS[signal]
    mask = df["is_us"]
    if spec.get("daily"):
        mask &= df["is_daily"]
    if "rank" in spec:
        mask &= df["rank"] == spec["rank"]
    if "rank_min" in spec:
        mask &= df["rank"] >= spec["rank_min"]
    if "rank_max" in spec:
        mask &= df["rank"] <= spec["rank_max"]
    if "eval_min" in spec:
        mask &= df["eval_pct"] >= spec["eval_min"]
    if "eval_max" in spec:
        mask &= df["eval_pct"] <= spec["eval_max"]
    if signal == "us_rank_8_10":
        mask &= df["rank"].between(8, 10)
    return df[mask].copy()


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


def simulate_budget(
    ok: pd.DataFrame,
    budget: float = 1000,
    per_trade: float | None = None,
    federal_rate: float = 0.24,
    ma_rate: float = 0.12,
    sec_fee: float = 0.000145,
) -> dict:
    """Simulate fixed budget with 2-month holds, recycling capital on sell."""
    sub = ok.copy()
    sub["email_date"] = pd.to_datetime(sub["email_date"]).dt.normalize()
    sub["sell_date"] = pd.to_datetime(sub["sell_date"], utc=True).dt.tz_localize(None).dt.normalize()
    sub["return_pct"] = sub["return_pct"].astype(float)

    def tax_on_gain(gain: float) -> float:
        if gain <= 0:
            return 0.0
        ma_tax = gain * ma_rate
        fed_tax = gain * federal_rate - ma_tax * federal_rate
        return fed_tax + ma_tax

    cash = budget
    lots: list[dict] = []
    trade_size = per_trade

    days = pd.date_range(sub["email_date"].min(), sub["sell_date"].max(), freq="D")
    for day in days:
        for lot in lots[:]:
            if lot["sell_date"] <= day:
                principal = lot["principal"]
                proceeds = principal * (1 + lot["return_pct"] / 100)
                fee = proceeds * sec_fee
                gain = proceeds - principal - fee
                cash += proceeds - fee - tax_on_gain(gain)
                lots.remove(lot)

        buys = sub[sub["email_date"] == day]
        if len(buys) == 0 or cash < 1:
            continue
        if trade_size is None:
            amount_each = cash / len(buys)
        else:
            amount_each = trade_size
        for _, row in buys.iterrows():
            if cash < amount_each:
                break
            cash -= amount_each
            lots.append(
                {
                    "sell_date": row["sell_date"],
                    "principal": amount_each,
                    "return_pct": row["return_pct"],
                }
            )

    for lot in lots:
        principal = lot["principal"]
        proceeds = principal * (1 + lot["return_pct"] / 100)
        fee = proceeds * sec_fee
        gain = proceeds - principal - fee
        cash += proceeds - fee - tax_on_gain(gain)

    net_profit = cash - budget
    return {
        "budget_usd": budget,
        "per_trade_usd": per_trade,
        "trades_available": len(sub),
        "end_balance_usd": round(cash, 2),
        "net_profit_usd": round(net_profit, 2),
        "net_return_pct": round(net_profit / budget * 100, 2),
        "tax_assumption": f"MA {ma_rate:.0%} + federal {federal_rate:.0%} on gains",
    }


def focused_signal_analysis(results: pd.DataFrame, hold_months: int, budget: float = 1000) -> dict:
    """Analyze all focused signals on completed backtest results."""
    ok = results[results["status"] == "ok"].copy()
    ok = enrich_positions(ok)
    ok["return_pct"] = ok["return_pct"].astype(float)
    ok["eval_pct"] = ok["eval_pct"].astype(float)
    ok["rank"] = ok["rank"].astype(int)

    out: dict = {"hold_months": hold_months, "signals": {}}
    for key, spec in FOCUSED_SIGNALS.items():
        sub = apply_focused_filter(ok, key)
        if len(sub) < 1:
            continue
        stats = portfolio_stats(sub)
        # Find best fixed $/trade for budget simulation
        best_net = -float("inf")
        best_pt = 10
        for pt in range(5, 101, 5):
            sim = simulate_budget(sub, budget=budget, per_trade=pt)
            if sim["net_profit_usd"] > best_net:
                best_net = sim["net_profit_usd"]
                best_pt = pt
        out["signals"][key] = {
            "label": spec["label"],
            "description": spec["description"],
            "stats": stats,
            "budget_sim_10usd": simulate_budget(sub, budget=budget, per_trade=10),
            "budget_sim_optimal": simulate_budget(sub, budget=budget, per_trade=best_pt),
            "optimal_per_trade_usd": best_pt,
        }
    return out


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


def compare_strategies(
    r1: pd.DataFrame,
    r2: pd.DataFrame,
    budget: float = 1000,
    per_trade_amounts: list[int] | None = None,
) -> dict:
    """Side-by-side comparison of all focused signals (1M vs 2M)."""
    if per_trade_amounts is None:
        per_trade_amounts = [75, 90, 100]
    f1 = focused_signal_analysis(r1, 1, budget=budget)
    f2 = focused_signal_analysis(r2, 2, budget=budget)

    rows: list[dict] = []
    for key in FOCUSED_SIGNALS:
        s1 = f1["signals"].get(key)
        s2 = f2["signals"].get(key)
        if not s1 and not s2:
            continue
        row: dict = {
            "signal": key,
            "label": FOCUSED_SIGNALS[key]["label"],
            "primary": key == PRIMARY_SIGNAL,
        }
        for hold, sig in [("1m", s1), ("2m", s2)]:
            if not sig:
                continue
            st = sig["stats"]
            row[f"{hold}_trades"] = st.get("positions", 0)
            row[f"{hold}_avg_return_pct"] = st.get("avg_return_pct")
            row[f"{hold}_win_rate_pct"] = st.get("win_rate_pct")
            row[f"{hold}_optimal_per_trade"] = sig.get("optimal_per_trade_usd")
            opt = sig.get("budget_sim_optimal", {})
            row[f"{hold}_net_profit_usd"] = opt.get("net_profit_usd")
            row[f"{hold}_net_return_pct"] = opt.get("net_return_pct")
        for pt in per_trade_amounts:
            sub2 = apply_focused_filter(
                enrich_positions(r2[r2["status"] == "ok"].copy()), key
            )
            if len(sub2):
                sim = simulate_budget(sub2, budget=budget, per_trade=pt)
                row[f"2m_budget_${pt}_net"] = sim["net_profit_usd"]
        rows.append(row)

    return {
        "budget_usd": budget,
        "per_trade_amounts": per_trade_amounts,
        "primary_signal": PRIMARY_SIGNAL,
        "strategies": rows,
    }


def print_strategy_comparison(comp: dict) -> None:
    print("\n" + "=" * 90)
    print(f"STRATEGY COMPARISON — $1,000 budget (primary: {comp['primary_signal']})")
    print("=" * 90)
    header = (
        f"{'Signal':<32} {'2M Trades':>9} {'2M Avg%':>8} {'2M Win%':>8} "
        f"{'2M Opt$':>7} {'2M Net$':>8} {'2M Net%':>7}"
    )
    print(header)
    print("-" * 90)
    for row in comp["strategies"]:
        marker = " *" if row.get("primary") else "  "
        print(
            f"{marker}{row['label'][:30]:<30} "
            f"{row.get('2m_trades', 0):>9} "
            f"{row.get('2m_avg_return_pct', 0) or 0:>7.1f}% "
            f"{row.get('2m_win_rate_pct', 0) or 0:>7.1f}% "
            f"{row.get('2m_optimal_per_trade', 0) or 0:>6.0f} "
            f"{row.get('2m_net_profit_usd', 0) or 0:>7.0f} "
            f"{row.get('2m_net_return_pct', 0) or 0:>+6.1f}%"
        )
    pts = comp.get("per_trade_amounts", [])
    if pts:
        print("\n2-month budget sim at fixed $/trade:")
        subheader = f"{'Signal':<32}" + "".join(f" ${p:>3}" for p in pts)
        print(subheader)
        print("-" * (32 + 5 * len(pts)))
        for row in comp["strategies"]:
            marker = " *" if row.get("primary") else "  "
            vals = "".join(
                f" {row.get(f'2m_budget_${p}_net', 0) or 0:>4.0f}" for p in pts
            )
            print(f"{marker}{row['label'][:30]:<30}{vals}")
    print("\n* = recommended primary signal")


def print_focused_summary(focused: dict) -> None:
    print("\n" + "=" * 70)
    print(f"FOCUSED SIGNALS — {focused['hold_months']}-MONTH HOLD ($1,000 budget, MA tax)")
    print("=" * 70)
    for key, sig in focused["signals"].items():
        s = sig["stats"]
        sim = sig["budget_sim_optimal"]
        print(f"\n[{key}] {sig['label']}")
        print(f"  {sig['description']}")
        print(f"  Trades: {s.get('positions', 0)} | Avg return: {s.get('avg_return_pct', 0):.1f}% | Win rate: {s.get('win_rate_pct', 0):.1f}%")
        print(
            f"  $1,000 budget @ ${sig['optimal_per_trade_usd']}/trade: "
            f"net ${sim['net_profit_usd']:.2f} ({sim['net_return_pct']:+.1f}%)"
        )


def main():
    parser = argparse.ArgumentParser(description="Pharma market movers backtest v2")
    parser.add_argument("--data", default=str(DATA_PATH))
    parser.add_argument(
        "--filter",
        choices=list(FOCUSED_SIGNALS),
        help="Run backtest on a focused signal subset only",
    )
    parser.add_argument(
        "--analyze-focused",
        action="store_true",
        help="Analyze focused signals from existing results (no price fetch)",
    )
    parser.add_argument("--budget", type=float, default=1000, help="Budget for simulations")
    parser.add_argument(
        "--compare-strategies",
        action="store_true",
        help="Compare all focused signals side-by-side (uses existing CSVs)",
    )
    args = parser.parse_args()

    OUTPUT_DIR.mkdir(exist_ok=True)

    if args.analyze_focused or args.compare_strategies:
        r1_path = OUTPUT_DIR / "results_1month.csv"
        r2_path = OUTPUT_DIR / "results_2month.csv"
        if not r1_path.exists() or not r2_path.exists():
            print("Results not found. Run full backtest first.")
            return
        r1 = pd.read_csv(r1_path)
        r2 = pd.read_csv(r2_path)
        if args.compare_strategies:
            comp = compare_strategies(r1, r2, budget=args.budget)
            with open(OUTPUT_DIR / "strategy_comparison.json", "w") as f:
                json.dump(comp, f, indent=2, default=str)
            print_strategy_comparison(comp)
            print(f"\nSaved: {OUTPUT_DIR / 'strategy_comparison.json'}")
            if not args.analyze_focused:
                return
        f1 = focused_signal_analysis(r1, 1, budget=args.budget)
        f2 = focused_signal_analysis(r2, 2, budget=args.budget)
        focused = {"1_month": f1, "2_month": f2}
        with open(OUTPUT_DIR / "focused_signals.json", "w") as f:
            json.dump(focused, f, indent=2, default=str)
        print_focused_summary(f2)
        print(f"\nSaved: {OUTPUT_DIR / 'focused_signals.json'}")
        return

    positions = load_positions(Path(args.data))
    if args.filter:
        positions = apply_focused_filter(positions, args.filter)
        print(f"Filter: {args.filter} ({FOCUSED_SIGNALS[args.filter]['label']})")
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
    f1 = focused_signal_analysis(r1, 1, budget=args.budget)
    f2 = focused_signal_analysis(r2, 2, budget=args.budget)
    full = {
        "1_month": s1,
        "2_month": s2,
        "comparison": comparison,
        "focused_signals": {"1_month": f1, "2_month": f2},
    }
    with open(OUTPUT_DIR / "summary_v2.json", "w") as f:
        json.dump(full, f, indent=2, default=str)
    with open(OUTPUT_DIR / "focused_signals.json", "w") as f:
        json.dump({"1_month": f1, "2_month": f2}, f, indent=2, default=str)

    print("\n" + "=" * 70)
    print("PHARMA BACKTEST V2 — 1 MONTH vs 2 MONTH")
    print("=" * 70)
    print(json.dumps(full, indent=2, default=str))
    print_focused_summary(f2)

    missing = sorted(positions.loc[positions["ticker"].isna(), "company"].unique())
    if missing:
        with open(OUTPUT_DIR / "missing_tickers.txt", "w") as f:
            f.write("\n".join(missing))
        print(f"\nMissing tickers ({len(missing)}): see {OUTPUT_DIR / 'missing_tickers.txt'}")


if __name__ == "__main__":
    main()
