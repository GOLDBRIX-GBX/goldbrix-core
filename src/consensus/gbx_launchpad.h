// Copyright (c) 2026 The GoldBrix developers
// Distributed under the MIT software license.
//
// GBX LAUNCHPAD IN CONSENSUS (IDEE V) — transition rules.
//
// A memecoin curve lives in exactly ONE UTXO whose value IS the reserve.
// The UTXO is a P2WSH of the canonical script:  <coin_id:32> OP_DROP OP_TRUE
// Anyone can spend it — but consensus dictates what the spending transaction
// must look like. No owner, no key, no server. The rules ARE the custodian.
#ifndef GBX_CONSENSUS_LAUNCHPAD_H
#define GBX_CONSENSUS_LAUNCHPAD_H

#include <consensus/gbx_curve.h>
#include <consensus/gbx_token.h>
#include <crypto/sha256.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <optional>
#include <vector>

namespace Consensus { struct Params; }

namespace gbx {

//! Curve operations declared in the OP_RETURN of a spending transaction.
enum class CurveOp : uint8_t { CREATE = 'C', BUY = 'B', SELL = 'S', REFUND = 'R', GRADUATE = 'G',
                              POOL_BUY = 'P', POOL_SELL = 'Q' };

//! A coin is born with a real position in it. Not a fee — the creator's own money, on the
//! same curve as everyone else's, at the same price. Spam costs; launching costs nothing extra.
static constexpr int64_t CURVE_MIN_DEV_BUY_SAT = 1;                 //!< IDEE W: no minimum in money. The first buy is mandatory (a real
                                                                    //!< position on the same curve as everyone else), but its size is the
                                                                    //!< creator's choice. The barrier to creating a coin is WORK, not capital:
                                                                    //!< a whale and a phone pay the same price — time. See CREATE_POW below.

//! ── IDEE W: CREATE COSTS WORK, NOT MONEY ──────────────────────────────────────
//! A coin is born only if its creator did real SHA256d work FOR THIS COIN.
//!
//!   proof = an 80-byte GoldBrix header whose merkle root IS the coin_id.
//!
//! Why this shape:
//!  · merkle == coin_id  → the proof is bound to this coin and no other. Change the
//!    coin and the hash changes, so the work must be redone. It cannot be reused,
//!    resold, or stolen: coin_id is the fingerprint of an outpoint only you can spend.
//!  · hashPrevBlock must be a RECENT block → work cannot be stockpiled in advance.
//!  · AuxPoW is FORBIDDEN here. A merged-mining pool produces proofs as a by-product
//!    at zero marginal cost; allowing it would let anyone BUY the barrier instead of
//!    doing it, which puts capital back in charge. The work must be done on purpose.
//!  · The difficulty of the proof is derived from the network's own difficulty, so it
//!    needs no retarget of its own: as the chain grows, creating stays proportionally
//!    the same amount of phone-work forever, at any price of GBX.
static constexpr size_t CREATE_POW_LEN = 80;                        //!< a bare block header
static constexpr int CREATE_POW_MAX_AGE = 100;                      //!< blocks: the proof must be fresh
static constexpr int CREATE_POW_SHIFT = 5;                          //!< target = network target << 5 (32x easier than a block: ~a few minutes of phone work). Calibrated s42 on measured hashrate; adjustable ONLY before activation.

//! The identity of a coin is not a name someone picked — it is the fingerprint of the very
//! output that funded its birth. An outpoint can be spent only once in history, so no two
//! curves can ever share an id, and nobody can squat on someone else's coin.
//!     coin_id = SHA256( txid || vout )  of the first input of the CREATE transaction
uint256 CurveIdFromOutpoint(const COutPoint& out);

//! ── GRADUATION ────────────────────────────────────────────────────────────────
//! When a curve fills, it dies and a pool is born. The reserve and the remaining
//! tokens become permanent liquidity: the pool has no owner and no LP tokens to
//! redeem, so the money can never be pulled out. There is nothing left to rug.
//!
//! The pool lives in its own UTXO, holding the GBX side. The token side is carried
//! in the script, exactly like a holding — so the node reads both reserves locally.
//!     poolScript = <coin_id:32> <tokens:8> OP_2DROP OP_TRUE      (anyone-can-spend,
//!     but consensus dictates the shape of the result: x*y=k, fee burned)
CScript PoolWitnessScript(const uint256& coin_id, int64_t tokens);
CScript PoolScriptPubKey(const uint256& coin_id, int64_t tokens);

//! Read the token side of a pool back out of the witness script revealed when spent.
std::optional<int64_t> ParsePoolWitnessScript(const std::vector<unsigned char>& ws, const uint256& coin_id);

//! Refund becomes available after this many blocks without any curve activity.
//! 3s blocks => 30 days = 864,000 blocks. A dead coin returns the money it holds.
static constexpr int CURVE_REFUND_IDLE_BLOCKS = 864000;

//! Below this, a reserve cannot be carried in an output (dust). The last refund closes
//! the curve and burns the remainder instead of stranding it forever.
static constexpr int64_t CURVE_DUST_SAT = 546;

//! Build the canonical witness script for a coin: <coin_id> OP_DROP OP_TRUE
//! IDEE X: the curve carries its own market memory. M = the largest trade (sat)
//! seen in the window, h_M = the height that set it. Both travel IN the reserve
//! UTXO itself — exactly like the pool carries its token side — so validation
//! reads them locally from the revealed witness script: no index, no server,
//! no key, reconstructible by anyone from the chain alone.
inline CScript CurveWitnessScript(const uint256& coin_id, int64_t m_max, uint32_t h_m)
{
    CScript s;
    s << std::vector<unsigned char>(coin_id.begin(), coin_id.end());
    std::vector<unsigned char> m(8), h(4);
    for (int i = 0; i < 8; ++i) m[i] = (unsigned char)((m_max >> (8*(7-i))) & 0xff);
    for (int i = 0; i < 4; ++i) h[i] = (unsigned char)((h_m  >> (8*(3-i))) & 0xff);
    s << m << h;
    s << OP_2DROP << OP_DROP << OP_TRUE;
    return s;
}

//! Canonical scriptPubKey (P2WSH) holding a coin's reserve.
inline CScript CurveScriptPubKey(const uint256& coin_id, int64_t m_max, uint32_t h_m)
{
    const CScript ws = CurveWitnessScript(coin_id, m_max, h_m);
    uint256 h;
    CSHA256().Write(ws.data(), ws.size()).Finalize(h.begin());
    CScript spk;
    spk << OP_0 << std::vector<unsigned char>(h.begin(), h.end());
    return spk;
}

//! Is this output a curve reserve? If yes, extract the coin_id.
//! Recognition is purely local: we cannot invert SHA256, so the coin_id is carried
//! in the OP_RETURN and verified by rebuilding the scriptPubKey from it.
//! IDEE X: a curve output can no longer be recognized from coin_id alone (its
//! script hash moves with M) — recognition happens at spend, from the revealed
//! witness script, exactly like the pool. See ParseCurveWitnessScript.
std::optional<std::pair<int64_t, uint32_t>> ParseCurveWitnessScript(
    const std::vector<unsigned char>& ws_bytes, const uint256& coin_id);

//! Parsed intent from the transaction's OP_RETURN.
struct CurveIntent {
    CurveOp op;
    uint256 coin_id;
    int64_t amount{0};        //!< BUY: gbx_in (gross) · SELL/REFUND: tokens burned back
    int64_t tokens_out{0};    //!< tokens minted to the payload pubkey (BUY) or returned as change (SELL/REFUND)
    std::vector<unsigned char> pubkey;  //!< 33 bytes: who receives the tokens
    std::vector<unsigned char> create_pow;  //!< IDEE W: 80 bytes, CREATE only. Empty for every other op.
};

//! IDEE W — is the attached proof of work valid for this coin?
//! Pure function: it is given the values, never the chain, so it stays trivially
//! deterministic and testable. The caller (validation) looks the previous block up.
//! @param[in] pow80        the 80-byte header carried by the CREATE
//! @param[in] coin_id      the coin being born (must equal the header's merkle root)
//! @param[in] prev_nbits   nBits of the block the proof was mined on
//! @param[in] prev_height  height of that block
//! @param[in] spend_height height at which the CREATE is being included
bool CheckCreatePoW(const std::vector<unsigned char>& pow80,
                    const uint256& coin_id,
                    unsigned int prev_nbits,
                    int prev_height,
                    int spend_height,
                    const Consensus::Params& params);

//! Extract the single curve intent from a transaction, if any.
//! Format: OP_RETURN "GBX:C:" <op:1> <coin_id:32> <amount:8> <tokens_out:8> <pubkey:33> (big-endian)
std::optional<CurveIntent> ParseCurveIntent(const CTransaction& tx);

//! Result of validating a curve transition.
enum class CurveError {
    OK = 0,
    NO_INTENT,          //!< spends a curve UTXO but declares nothing
    BAD_TOKENS,         //!< tokens conjured, destroyed, or not proven to be held
    BAD_COIN_ID,        //!< the coin id is not the fingerprint of the funding outpoint
    NOT_GRADUATED,      //!< graduation attempted before the reserve was full
    BAD_OUTPUT,         //!< the new curve UTXO is missing or malformed
    BAD_AMOUNT,         //!< the value moved does not match the formula
    BAD_FEE,            //!< the protocol fee was not burned
    NOT_IDLE,           //!< refund attempted on a coin that is still alive
    CURVE_EXHAUSTED,    //!< buy past the curve supply: must graduate instead
    MULTIPLE_CURVES,    //!< more than one curve UTXO spent in one transaction
};

//! The heart of IDEE V.
//!
//! Called for any transaction that spends a curve UTXO. Given the reserve held by
//! the spent UTXO, the intent declared in the OP_RETURN and the transaction's own
//! outputs, decide whether the transition is legal. Nobody can take a single unit out
//! of a reserve except by the formula — not the creator, not the founder, nobody.
//!
//! @param[in] reserve_in       value of the spent curve UTXO (sat)
//! @param[in] curve_height     height at which the spent curve UTXO was created
//! @param[in] spend_height     height of the block spending it
CurveError CheckCurveTransition(const CTransaction& tx,
                                const CurveIntent& intent,
                                int64_t reserve_in,
                                int64_t m_in,
                                uint32_t hm_in,
                                int curve_height,
                                int spend_height,
                                int refund_idle_blocks,
                                int grad_window_blocks);

//! Validate a trade against a graduated pool. Same philosophy: the pool has no owner,
//! so the only thing that can move its money is the constant product itself.
//! @param[in] pool_gbx_in   GBX held by the spent pool output
//! @param[in] pool_tok_in   token side, read from the revealed pool script
CurveError CheckPoolTransition(const CTransaction& tx,
                               const CurveIntent& intent,
                               int64_t pool_gbx_in,
                               int64_t pool_tok_in);

//! Burn address script (unspendable): OP_RETURN-less, provably unspendable OP_0 to the
//! canonical burn program. Fees must go here — the protocol collects nothing.
CScript CurveBurnScript();

} // namespace gbx

class CCoinsViewCache;
class TxValidationState;

namespace gbx {

//! Consensus entry point (IDEE V).
//!
//! Called right after Consensus::CheckTxInputs. If the transaction spends a curve
//! reserve, the transition must obey the curve — otherwise the transaction is invalid.
//! Before the activation height this is a no-op, so old blocks stay valid forever.
[[nodiscard]] bool CheckCurveInputs(const CTransaction& tx,
                                    TxValidationState& state,
                                    const CCoinsViewCache& inputs,
                                    int nSpendHeight,
                                    const Consensus::Params& params);

} // namespace gbx

#endif // GBX_CONSENSUS_LAUNCHPAD_H
