// Copyright (c) 2026 The GoldBrix developers
// Distributed under the MIT software license.
//
// GBX LAUNCHPAD IN CONSENSUS (IDEE V) — bonding curve, integer-only.
//
// Design: the curve of each coin lives in ONE UTXO ("curve UTXO"). Its value IS
// the reserve. Tokens sold are a pure function of the reserve, so no global state
// is required: every node can validate a curve transition from the spent coin and
// the new output alone.
//
// All arithmetic is integer. Floating point is FORBIDDEN in consensus code:
// two nodes must reach bit-identical results or the chain forks.
#ifndef GBX_CONSENSUS_CURVE_H
#define GBX_CONSENSUS_CURVE_H

#include <consensus/amount.h>
#include <cstdint>

namespace gbx {

//! Virtual reserves defining the curve (identical to the values proven in production).
static constexpr int64_t CURVE_V_GBX_SAT = 30000LL * COIN;      //!< 30,000 GBX in sat
static constexpr int64_t CURVE_V_TOKENS  = 1073000000LL;        //!< virtual token reserve
static constexpr int64_t CURVE_TOKENS    = 800000000LL;         //!< real tokens sellable on the curve
static constexpr int64_t CURVE_LP_TOKENS = 200000000LL;         //!< tokens reserved for the AMM pool
// ── IDEE X: GRADUATION BY THE COIN'S OWN ACTIVITY ─────────────────────────────
//! A fixed threshold in money breaks at any large price move (too high and no
//! coin ever graduates; too low and it filters nothing) — the same disease
//! CURVE_MIN_DEV_BUY had, cured the same way: no absolute money value anywhere.
//! What the pool must guarantee is DEPTH RELATIVE TO THE COIN'S OWN TRADES:
//! slippage of an AMM x*y=k on a trade m against reserve R is m/R, so demanding
//! R >= N*M (M = the largest trade seen in the last K blocks) caps the slippage
//! of the biggest trade this market has ever produced at 1/N — at ANY price of
//! GBX, forever, with no oracle. A whale cannot buy the graduation cheaply: her
//! own giant buy becomes the new M and raises the bar she must clear (a single
//! buy can never exceed R/(N-1) without postponing its own graduation).
static constexpr int64_t CURVE_GRAD_DEPTH_N   = 20;             //!< pool >= N x the largest recent trade -> slippage <= 1/N (5%)
static constexpr int64_t CURVE_GRAD_MIN_SAT   = 2000LL * COIN;  //!< absolute floor, anti-degenerate only: a pool of pocket dust serves nobody
//!  (the window K lives in Consensus::Params — real on mainnet, small on regtest, like the refund window)
//! FOUNDER-LOCKED 2026-07-15 (s44): N=20, K=201600, R_MIN=2000 GBX — confirmed on the
//! proven numbers (23/23 unit, 6/6 live). Immutable after R.
static constexpr int64_t CURVE_GRADUATION_SAT = 80000LL * COIN;  //!< legacy fixed threshold (superseded by IDEE X; kept for the full-curve arithmetic)
static constexpr int64_t POOL_FEE_BPS = 30;                      //!< 0.30% on AMM trades — BURNED, never collected
static constexpr int64_t CURVE_FEE_BPS   = 50;                  //!< 0.50% on buy/sell — BURNED, never collected

//! k = V_GBX_SAT * V_TOKENS = 3.219e21 — exceeds int64, must be 128-bit.
using u128 = __uint128_t;
static constexpr u128 CURVE_K = (u128)CURVE_V_GBX_SAT * (u128)CURVE_V_TOKENS;

//! Tokens emitted for a given reserve. Floor division => deterministic on every node.
inline int64_t CurveTokensSold(int64_t reserve_sat)
{
    if (reserve_sat < 0) return 0;
    const u128 gbx_res = (u128)CURVE_V_GBX_SAT + (u128)reserve_sat;
    const int64_t tok_res = (int64_t)(CURVE_K / gbx_res);
    return CURVE_V_TOKENS - tok_res;
}

//! Tokens a buyer receives for gbx_in_sat added to the reserve (fee already deducted).
//! Returns false if the trade is not allowed (bad amount, curve exhausted).
inline bool CurveBuy(int64_t reserve_sat, int64_t gbx_in_sat, int64_t& tokens_out, int64_t& new_reserve_sat)
{
    if (gbx_in_sat <= 0 || reserve_sat < 0) return false;
    if (gbx_in_sat > MAX_MONEY || reserve_sat > MAX_MONEY) return false;
    const u128 cur_gbx = (u128)CURVE_V_GBX_SAT + (u128)reserve_sat;
    const u128 new_gbx = cur_gbx + (u128)gbx_in_sat;
    const int64_t cur_tok = (int64_t)(CURVE_K / cur_gbx);
    const int64_t new_tok = (int64_t)(CURVE_K / new_gbx);
    tokens_out = cur_tok - new_tok;                    // monotone: always >= 0
    if (tokens_out <= 0) return false;                 // dust buy that moves nothing
    const int64_t sold = CURVE_V_TOKENS - cur_tok;
    if (sold + tokens_out > CURVE_TOKENS) return false; // curve supply exhausted -> graduation
    new_reserve_sat = reserve_sat + gbx_in_sat;
    return true;
}

//! GBX (gross, before fee) a seller receives for tokens_in burned back into the curve.
inline bool CurveSell(int64_t reserve_sat, int64_t tokens_in, int64_t& gbx_out_sat, int64_t& new_reserve_sat)
{
    if (tokens_in <= 0 || reserve_sat <= 0) return false;
    if (tokens_in > CURVE_V_TOKENS) return false;
    const u128 cur_gbx = (u128)CURVE_V_GBX_SAT + (u128)reserve_sat;
    const int64_t cur_tok = (int64_t)(CURVE_K / cur_gbx);
    const int64_t sold = CURVE_V_TOKENS - cur_tok;
    if (tokens_in > sold) return false;                 // cannot sell more than was ever emitted
    const u128 new_tok = (u128)cur_tok + (u128)tokens_in;
    const int64_t new_gbx = (int64_t)(CURVE_K / new_tok);
    gbx_out_sat = (int64_t)cur_gbx - new_gbx;          // floor => dust stays in the reserve (safe direction)
    if (gbx_out_sat < 0) return false;
    if (gbx_out_sat > reserve_sat) gbx_out_sat = reserve_sat; // hard guard: never drain beyond what exists
    new_reserve_sat = reserve_sat - gbx_out_sat;
    return true;
}

//! ── AMM (post-graduation) ─────────────────────────────────────────────────────
//! Constant product on REAL reserves. No virtual anything, no owner, no LP tokens:
//! the liquidity was locked the moment the coin graduated and can never be withdrawn.

//! Tokens out for gbx in (fee already deducted). x*y=k, floor division.
inline bool PoolBuy(int64_t pool_gbx, int64_t pool_tok, int64_t gbx_in, int64_t& tokens_out,
                    int64_t& new_gbx, int64_t& new_tok)
{
    if (pool_gbx <= 0 || pool_tok <= 0 || gbx_in <= 0) return false;
    if (gbx_in > MAX_MONEY || pool_gbx > MAX_MONEY) return false;
    const u128 k = (u128)pool_gbx * (u128)pool_tok;
    const u128 ng = (u128)pool_gbx + (u128)gbx_in;
    // CEILING on the token side too: the pool keeps the dust in TOKENS, so the buyer gets
    // slightly fewer. With floor, k grew on every buy and a round-trip printed free money.
    // Rounding must always favour the pool — on both legs, or the invariant leaks.
    const int64_t nt = (int64_t)((k + ng - 1) / ng);
    tokens_out = pool_tok - nt;
    if (tokens_out <= 0) return false;
    new_gbx = pool_gbx + gbx_in;
    new_tok = nt;
    return true;
}

//! GBX out (gross, before fee) for tokens in.
inline bool PoolSell(int64_t pool_gbx, int64_t pool_tok, int64_t tok_in, int64_t& gbx_out,
                     int64_t& new_gbx, int64_t& new_tok)
{
    if (pool_gbx <= 0 || pool_tok <= 0 || tok_in <= 0) return false;
    const u128 k = (u128)pool_gbx * (u128)pool_tok;
    const u128 nt = (u128)pool_tok + (u128)tok_in;
    // CEILING, not floor: the rounding must always favour the pool, never the trader.
    // With floor, a buy followed by a sell would return MORE than was paid in — money from
    // nothing, and the pool bleeds out one unit at a time. Rounding up keeps k monotone.
    const int64_t ng = (int64_t)((k + nt - 1) / nt);
    gbx_out = pool_gbx - ng;
    if (gbx_out <= 0) return false;
    if (gbx_out > pool_gbx) return false;             // cannot drain more than exists
    new_gbx = ng;
    new_tok = (int64_t)nt;
    return true;
}

//! Pool fee (burned). Integer, floor.
inline int64_t PoolFee(int64_t gross_sat)
{
    if (gross_sat <= 0) return 0;
    return (int64_t)(((u128)gross_sat * (u128)POOL_FEE_BPS) / 10000);
}

//! Protocol fee (burned). Integer, floor.
inline int64_t CurveFee(int64_t gross_sat)
{
    if (gross_sat <= 0) return 0;
    return (int64_t)(((u128)gross_sat * (u128)CURVE_FEE_BPS) / 10000);
}

} // namespace gbx

#endif // GBX_CONSENSUS_CURVE_H
