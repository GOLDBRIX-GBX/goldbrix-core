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

namespace gbx {

//! Curve operations declared in the OP_RETURN of a spending transaction.
enum class CurveOp : uint8_t { CREATE = 'C', BUY = 'B', SELL = 'S', REFUND = 'R', GRADUATE = 'G' };

//! A coin is born with a real position in it. Not a fee — the creator's own money, on the
//! same curve as everyone else's, at the same price. Spam costs; launching costs nothing extra.
static constexpr int64_t CURVE_MIN_DEV_BUY_SAT = 1 * 100000000LL;   //!< 1 GBX minimum first buy

//! The identity of a coin is not a name someone picked — it is the fingerprint of the very
//! output that funded its birth. An outpoint can be spent only once in history, so no two
//! curves can ever share an id, and nobody can squat on someone else's coin.
//!     coin_id = SHA256( txid || vout )  of the first input of the CREATE transaction
uint256 CurveIdFromOutpoint(const COutPoint& out);

//! Refund becomes available after this many blocks without any curve activity.
//! 3s blocks => 30 days = 864,000 blocks. A dead coin returns the money it holds.
static constexpr int CURVE_REFUND_IDLE_BLOCKS = 864000;

//! Below this, a reserve cannot be carried in an output (dust). The last refund closes
//! the curve and burns the remainder instead of stranding it forever.
static constexpr int64_t CURVE_DUST_SAT = 546;

//! Build the canonical witness script for a coin: <coin_id> OP_DROP OP_TRUE
inline CScript CurveWitnessScript(const uint256& coin_id)
{
    CScript s;
    s << std::vector<unsigned char>(coin_id.begin(), coin_id.end());
    s << OP_DROP << OP_TRUE;
    return s;
}

//! Canonical scriptPubKey (P2WSH) holding a coin's reserve.
inline CScript CurveScriptPubKey(const uint256& coin_id)
{
    const CScript ws = CurveWitnessScript(coin_id);
    uint256 h;
    CSHA256().Write(ws.data(), ws.size()).Finalize(h.begin());
    CScript spk;
    spk << OP_0 << std::vector<unsigned char>(h.begin(), h.end());
    return spk;
}

//! Is this output a curve reserve? If yes, extract the coin_id.
//! Recognition is purely local: we cannot invert SHA256, so the coin_id is carried
//! in the OP_RETURN and verified by rebuilding the scriptPubKey from it.
inline bool IsCurveOutput(const CTxOut& out, const uint256& coin_id)
{
    return out.scriptPubKey == CurveScriptPubKey(coin_id);
}

//! Parsed intent from the transaction's OP_RETURN.
struct CurveIntent {
    CurveOp op;
    uint256 coin_id;
    int64_t amount{0};        //!< BUY: gbx_in (gross) · SELL/REFUND: tokens burned back
    int64_t tokens_out{0};    //!< tokens minted to the payload pubkey (BUY) or returned as change (SELL/REFUND)
    std::vector<unsigned char> pubkey;  //!< 33 bytes: who receives the tokens
};

//! Extract the single curve intent from a transaction, if any.
//! Format: OP_RETURN "GBX:C:" <op:1> <coin_id:32> <amount:8> <tokens_out:8> <pubkey:33> (big-endian)
std::optional<CurveIntent> ParseCurveIntent(const CTransaction& tx);

//! Result of validating a curve transition.
enum class CurveError {
    OK = 0,
    NO_INTENT,          //!< spends a curve UTXO but declares nothing
    BAD_TOKENS,         //!< tokens conjured, destroyed, or not proven to be held
    BAD_COIN_ID,        //!< the coin id is not the fingerprint of the funding outpoint
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
                                int curve_height,
                                int spend_height);

//! Burn address script (unspendable): OP_RETURN-less, provably unspendable OP_0 to the
//! canonical burn program. Fees must go here — the protocol collects nothing.
CScript CurveBurnScript();

} // namespace gbx

namespace Consensus { struct Params; }
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
