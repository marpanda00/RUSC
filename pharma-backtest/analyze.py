#!/usr/bin/env python3
"""Generate charts and extended analysis from backtest results."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy import stats

OUTPUT = Path(__file__).parent / "output"
RESULTS = OUTPUT / "backtest_results.csv"


def main():
    df = pd.read_csv(RESULTS)
    ok = df[df["status"] == "ok"].copy()
    ok["eval_pct"] = ok["eval_pct"].astype(float)
    ok["return_pct"] = ok["return_pct"].astype(float)

    # Scatter: eval vs return
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))

    ax = axes[0, 0]
    ax.scatter(ok["eval_pct"], ok["return_pct"], alpha=0.4, s=20)
    z = np.polyfit(ok["eval_pct"], ok["return_pct"], 1)
    p = np.poly1d(z)
    xs = np.linspace(ok["eval_pct"].min(), ok["eval_pct"].max(), 100)
    ax.plot(xs, p(xs), "r--", linewidth=2, label=f"Trend (r={stats.pearsonr(ok['eval_pct'], ok['return_pct'])[0]:.2f})")
    ax.axhline(0, color="gray", linewidth=0.8)
    ax.set_xlabel("Evaluation score (%)")
    ax.set_ylabel("1-month return (%)")
    ax.set_title("Eval score vs 1-month return")
    ax.legend()

    # Bucket bar chart
    bins = [0, 10, 20, 30, 50, 100, 1000]
    labels = ["0-10%", "10-20%", "20-30%", "30-50%", "50-100%", "100%+"]
    ok["eval_bucket"] = pd.cut(ok["eval_pct"], bins=bins, labels=labels, right=True)
    bucket = ok.groupby("eval_bucket", observed=True)["return_pct"].mean()
    ax = axes[0, 1]
    colors = ["#2ecc71" if v > 0 else "#e74c3c" for v in bucket.values]
    bucket.plot(kind="bar", ax=ax, color=colors)
    ax.axhline(0, color="gray", linewidth=0.8)
    ax.set_title("Avg 1-month return by eval bucket")
    ax.set_ylabel("Return (%)")
    ax.tick_params(axis="x", rotation=45)

    # By rank
    ax = axes[1, 0]
    rank_avg = ok.groupby("rank")["return_pct"].mean()
    rank_avg.plot(kind="bar", ax=ax, color=["#3498db", "#9b59b6", "#1abc9c"])
    ax.axhline(0, color="gray", linewidth=0.8)
    ax.set_title("Avg return by rank (company_1/2/3)")
    ax.set_ylabel("Return (%)")
    ax.set_xticklabels(["#1 (top eval)", "#2", "#3"], rotation=0)

    # Cumulative portfolio
    ax = axes[1, 1]
    ok_sorted = ok.sort_values("email_date")
    ok_sorted["cum_end_value"] = ok_sorted["end_value_usd"].cumsum()
    ok_sorted["cum_invested"] = range(1, len(ok_sorted) + 1)
    ax.plot(ok_sorted["email_date"], ok_sorted["cum_end_value"], label="Portfolio value")
    ax.plot(ok_sorted["email_date"], ok_sorted["cum_invested"], "--", label="Amount invested", color="gray")
    ax.set_title("Cumulative $1-per-position portfolio")
    ax.set_ylabel("USD")
    ax.legend()
    ax.tick_params(axis="x", rotation=45)

    plt.tight_layout()
    plt.savefig(OUTPUT / "backtest_charts.png", dpi=150)
    print(f"Saved {OUTPUT / 'backtest_charts.png'}")

    # Per-email: does highest eval win?
    email_groups = []
    for (date, subject), g in ok.groupby(["email_date", "email_subject"]):
        if len(g) < 2:
            continue
        best_return_idx = g["return_pct"].idxmax()
        best_eval_idx = g["eval_pct"].idxmax()
        email_groups.append(
            {
                "email_date": date,
                "n_companies": len(g),
                "top_eval_return": g.loc[best_eval_idx, "return_pct"],
                "best_return": g["return_pct"].max(),
                "top_eval_was_best": best_return_idx == best_eval_idx,
                "avg_return": g["return_pct"].mean(),
            }
        )
    email_df = pd.DataFrame(email_groups)
    top_eval_wins = email_df["top_eval_was_best"].mean() * 100
    print(f"\nTop-eval company had best 1-month return in {top_eval_wins:.1f}% of emails ({len(email_df)} emails with 2+ companies)")

    # Repeated mentions
    company_stats = (
        ok.groupby("company")
        .agg(mentions=("return_pct", "count"), avg_return=("return_pct", "mean"), avg_eval=("eval_pct", "mean"))
        .sort_values("mentions", ascending=False)
    )
    repeat = company_stats[company_stats["mentions"] >= 3]
    repeat.to_csv(OUTPUT / "repeat_companies.csv")
    print(f"Companies mentioned 3+ times: {len(repeat)}")


if __name__ == "__main__":
    main()
