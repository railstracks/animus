---
name: market-open-assessment
title: Market Open Assessment
category: finance
tags: [markets, trading, analysis, intraday, report]
version: 1.0.0
description: >
  Produce a structured market assessment 30 minutes after market open.
  Pulls live market data, scans news and social channels, and delivers
  a positioned report with directional bias and key levels.
---

# Market Open Assessment

## Purpose

Assess market conditions approximately 30 minutes after the opening bell
to inform whether **long** or **short** positions are favorable for the
session. The report synthesizes technical data, news flow, and social
signal into a single actionable brief.

This procedure is **instrument-agnostic**. It applies equally to equity
indices, FX pairs, commodities, or crypto. Adapt the data sources and
parameters to the target instrument.

## When to Run

- **Primary:** 30 minutes after the target market's opening bell
- **Secondary (midday check):** Scheduled at the session midpoint to
  reassess conditions and flag whether position adjustments are needed.
  Use the same structure; emphasize what changed since the open.

## Inputs

Before starting, confirm you have:

1. **Target instrument** — ticker symbol or pair (e.g., SPX, BTC-EUR, EUR/USD)
2. **Data access** — a tool or API to pull current price, OHLC, and key
   technical levels (moving averages, prior day high/low, VWAP)
3. **News access** — web search capability for financial headlines
4. **Channel access** — `channels` tool for scanning social sentiment
   (Bluesky, Twitter/X, or any configured social channel)

## Procedure

### Phase 1 — Data Collection

**1.1 Pull market data**

Fetch the following for the target instrument:

- Current price and today's opening price
- Today's intraday high and low (so far)
- Previous day's high, low, and close
- Key moving averages (e.g., 15m/1h/4h/1D — adapt to instrument)
- Prior session VWAP if available
- Any major support/resistance levels from the daily chart

Record the timestamp of the data snapshot. Stale data is worse than no
data — always note the effective time.

**1.2 Scan news and headlines**

Search for recent (last 12 hours) headlines affecting:

- The target instrument directly
- The broader market or sector it belongs to
- Major economic events scheduled today (rate decisions, CPI, earnings)
- Geopolitical developments involving major economies relevant to the
  instrument
- Regulatory or legislative changes affecting the sector

Use `web_search` with queries like:
- `"{instrument}" market today`
- `"{sector}" stocks {date}`
- `economic calendar {date}`
- `"{relevant political figures}" market policy`

**1.3 Scan social channels**

Use the `channels` tool to pull recent messages from configured social
channels (Bluesky, Twitter/X, etc.). Look for:

- Sentiment direction (bullish/bearish/neutral chatter volume)
- Breaking information not yet in mainstream news
- Major accounts or analysts shifting their stance
- Unusual activity spikes that could signal emerging moves

Filter for signal, not noise. A single credible analyst changing
position is worth more than fifty generic "to the moon" posts.

### Phase 2 — Analysis

**2.1 Technical read**

Synthesize the market data into a regime classification:

- **Bullish** — price above key MAs, making higher highs/lows,
  momentum expanding
- **Bullish but compressed** — above key levels but stalling at
  resistance, momentum decelerating
- **Neutral / ranging** — chopping between support and resistance, no
  directional conviction
- **Bearish but compressed** — below key levels but finding support,
  selling pressure fading
- **Bearish** — price below key MAs, making lower highs/lows,
  momentum expanding downward

Identify the nearest support and resistance zones with specific price
levels. Note which levels are intraday vs. structural (daily/weekly).

**2.2 News and sentiment overlay**

Assess whether the fundamental backdrop supports or contradicts the
technical read:

- Does the news flow align with the technical regime?
- Are there scheduled events today that could override technicals?
- Is social sentiment confirming the price action, or diverging?
- Are there geopolitical risks that could cause sudden reversals?

Weight fundamental factors higher when:
- Major central bank events are scheduled
- Geopolitical tensions are escalating
- Regulatory decisions are pending
- Earnings releases for sector leaders are due

**2.3 Scenario construction**

Build three scenarios with probability estimates:

| Scenario | Probability | Price range | Trigger |
|----------|-------------|-------------|---------|
| Base case | ~50-60% | most likely range | the default path if nothing surprising happens |
| Bull case | ~15-25% | upside range | what would need to happen for upside breakout |
| Bear case | ~15-25% | downside range | what would need to happen for downside breakdown |

Probabilities must sum to roughly 100%. Be honest about uncertainty —
if the setup is genuinely unclear, say so and weight base/neutral
higher.

### Phase 3 — Report

**3.1 Structure**

Write the report using this structure:

```
# {INSTRUMENT} Market Assessment

- Market: {instrument}
- Report timestamp: {ISO timestamp with timezone}
- Data effective time: {ISO timestamp of data snapshot}
- Confidence: {Low/Medium/High}
- Forecast horizon: {e.g., next 1 hour, rest of session}

## Current Read
{2-4 paragraphs synthesizing the technical regime, momentum, and
key observations. State the directional bias clearly.}

## Key Levels
- Support: {levels with prices}
- Resistance: {levels with prices}
- Key observations about price action structure

## News and Sentiment
{Summary of relevant headlines, events, and social sentiment.
Highlight anything that could move the market today.}

## Scenarios
- Base case: {%} — {price range} — {description}
- Bull case: {%} — {price range} — {description}
- Bear case: {%} — {price range} — {description}

## Directional Bias
{One clear sentence: LONG / SHORT / NEUTRAL with conviction level
and primary reasoning.}

## Conclusion
{2-3 sentences summarizing the call and what would invalidate it.}
```

**3.2 Confidence calibration**

- **High:** Clear technical setup with confirming news/sentiment, no
  major scheduled events that could override
- **Medium:** Technical setup is present but mixed signals from news or
  momentum, or scheduled events add uncertainty
- **Low:** Conflicting signals across technicals and fundamentals, low
  volume, or major event risk pending

**3.3 Midday check-in variant**

When running the midday reassessment (halfway through the session):

- Open by referencing the morning report explicitly
- Lead with **"What changed since the open"**
- Re-evaluate all three scenarios — did the base case hold?
- If the directional bias has shifted, state this clearly and explain why
- Recommend **HOLD** / **ADJUST** / **EXIT** with reasoning
- Keep it shorter than the morning report — focus on delta, not full reanalysis

## Guidelines

- **Be specific with prices.** "Support around 4,520" not "support nearby."
- **Show your work.** If momentum is positive, state which timeframe and measure.
- **Acknowledge what you don't know.** Unscheduled news can override any analysis.
- **Don't fight the tape without evidence.** If price action contradicts the news, price action wins until it doesn't.
- **Time-stamp everything.** Market assessments expire. A report without an effective time is useless.
- **Prior reports matter.** When available, reference the prior assessment and evaluate whether the previous call was right. Track accuracy over time.
- **Instrument-agnostic.** This procedure works for any market. Adapt terminology (VWAP for equities, funding rates for crypto, yield levels for bonds) but keep the analytical framework constant.
