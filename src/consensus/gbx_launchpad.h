// Copyright (c) 2026 The GoldBrix developers
// Distributed under the MIT software license.
//
// GBX LAUNCHPAD IN CONSENSUS (IDEE V) — transition rules.
//
// A memecoin curve lives in exactly ONE UTXO whose value IS the reserve.
// The UTXO is a P2WSH of the canonical script:  <coin_id:32> OP_DROP OP_TRUE
// Anyone can spend it — but consensus dictates what the spending transaction
// must look like. No owner, no key, no server. The rules ARE the custodian.
#ifndef BITCOIN_CONSENSUS_GBX_LAUNCHPAD_H
#define BITCOIN_CONSENSUS_GBX_LAUNCHPAD_H

#include <consensus/gbx_curve.h>
#include <crypto/sha256.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <optional>
#include <vector>

namespace gbx {

//! Curve operations declared in the OP_RETURN of a spending transaction.
enum class CurveOp : uint8_t { BUY = 'B', SELL = 'S', REFUND = 'R', GRADUATE = 'G' };

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
    int64_t amount{0};   //!< BUY: gbx_in (sat, gross) · SELL/REFUND: tokens burned · GRADUATE: unused
};

//! Extract the single curve intent from a transaction, if any.
//! Format: OP_RETURN "GBX:C:" <op:1> <coin_id:32> <amount:8, big-endian>
std::optional<CurveIntent> ParseCurveIntent(const CTransaction& tx);

//! Result of validating a curve transition.
enum class CurveError {
    OK = 0,
    NO_INTENT,          //!< spends a curve UTXO but declares nothing
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
//! outputs, decide whether the transition is legal. Nobody can take a satoshi out
//! of a reserve except by the formula — not the creator, not the founder, nobody.
//!
//! @param[in] reserve_in       value of the spent curve UTXO (satoshi)
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

#endif // BITCOIN_CONSENSUS_GBX_LAUNCHPAD_H
