// Copyright (c) 2026 The GoldBrix developers
// Distributed under the MIT software license.

#include <consensus/gbx_launchpad.h>

#include <cstring>

namespace gbx {

//! Wire format of the intent, carried in a single OP_RETURN:
//!   "GBX:C:" (6 bytes) | op (1) | coin_id (32) | amount (8, big-endian) = 47 bytes
static const char* CURVE_TAG = "GBX:C:";
static constexpr size_t CURVE_TAG_LEN = 6;
static constexpr size_t CURVE_PAYLOAD_LEN = CURVE_TAG_LEN + 1 + 32 + 8; // 47

static bool IsKnownOp(unsigned char c)
{
    return c == (unsigned char)CurveOp::BUY || c == (unsigned char)CurveOp::SELL ||
           c == (unsigned char)CurveOp::REFUND || c == (unsigned char)CurveOp::GRADUATE;
}

std::optional<CurveIntent> ParseCurveIntent(const CTransaction& tx)
{
    std::optional<CurveIntent> found;

    for (const CTxOut& out : tx.vout) {
        const CScript& spk = out.scriptPubKey;
        if (spk.empty() || spk[0] != OP_RETURN) continue;

        // Read the single pushdata that follows OP_RETURN.
        CScript::const_iterator pc = spk.begin() + 1;
        opcodetype opcode;
        std::vector<unsigned char> data;
        if (!spk.GetOp(pc, opcode, data)) continue;
        if (data.size() != CURVE_PAYLOAD_LEN) continue;
        if (std::memcmp(data.data(), CURVE_TAG, CURVE_TAG_LEN) != 0) continue;

        const unsigned char op_byte = data[CURVE_TAG_LEN];
        if (!IsKnownOp(op_byte)) continue;

        // Two curve intents in one transaction = ambiguous. Reject the whole thing.
        if (found.has_value()) return std::nullopt;

        CurveIntent intent;
        intent.op = static_cast<CurveOp>(op_byte);
        std::memcpy(intent.coin_id.begin(), data.data() + CURVE_TAG_LEN + 1, 32);

        int64_t amount = 0;
        const unsigned char* a = data.data() + CURVE_TAG_LEN + 1 + 32;
        for (int i = 0; i < 8; ++i) amount = (amount << 8) | (int64_t)a[i]; // big-endian
        if (amount < 0) return std::nullopt;                                 // no negatives, ever
        intent.amount = amount;

        found = intent;
    }
    return found;
}


//! The canonical burn output: P2WPKH with an all-zero witness program.
//! No private key can ever produce this hash160. Fees leave the money supply forever.
CScript CurveBurnScript()
{
    CScript spk;
    spk << OP_0 << std::vector<unsigned char>(20, 0x00);
    return spk;
}

//! Sum of all outputs paying exactly to the burn script.
static int64_t BurnedAmount(const CTransaction& tx)
{
    const CScript burn = CurveBurnScript();
    int64_t total = 0;
    for (const CTxOut& out : tx.vout) {
        if (out.scriptPubKey == burn) total += out.nValue;
    }
    return total;
}

//! Value of the (single) curve output for this coin in the transaction.
//! Returns -1 if there is more than one; 0 if there is none (the curve is being closed).
static int64_t CurveOutputValue(const CTransaction& tx, const uint256& coin_id)
{
    const CScript spk = CurveScriptPubKey(coin_id);
    int64_t value = 0;
    int found = 0;
    for (const CTxOut& out : tx.vout) {
        if (out.scriptPubKey != spk) continue;
        if (++found > 1) return -1;   // two curve outputs = malformed
        value = out.nValue;
    }
    return value;   // 0 with found==0 means: no curve output -> the curve is closed
}

CurveError CheckCurveTransition(const CTransaction& tx,
                                const CurveIntent& intent,
                                int64_t reserve_in,
                                int curve_height,
                                int spend_height)
{
    if (reserve_in < 0) return CurveError::BAD_AMOUNT;

    const int64_t reserve_out = CurveOutputValue(tx, intent.coin_id);
    const int64_t burned = BurnedAmount(tx);

    switch (intent.op) {

    case CurveOp::BUY: {
        // intent.amount = gross GBX the buyer commits (satoshi).
        const int64_t fee = CurveFee(intent.amount);
        const int64_t net = intent.amount - fee;
        int64_t tokens_out = 0, expected_reserve = 0;
        if (!CurveBuy(reserve_in, net, tokens_out, expected_reserve)) return CurveError::CURVE_EXHAUSTED;
        if (expected_reserve <= 0) return CurveError::BAD_AMOUNT;      // a buy always leaves money behind
        if (reserve_out != expected_reserve) return CurveError::BAD_AMOUNT;
        if (burned < fee) return CurveError::BAD_FEE;   // the fee is burned, never collected
        return CurveError::OK;
    }

    case CurveOp::SELL: {
        // intent.amount = tokens burned back into the curve.
        int64_t gross = 0, expected_reserve = 0;
        if (!CurveSell(reserve_in, intent.amount, gross, expected_reserve)) return CurveError::BAD_AMOUNT;
        if (reserve_out != expected_reserve) return CurveError::BAD_AMOUNT;
        const int64_t fee = CurveFee(gross);
        if (burned < fee) return CurveError::BAD_FEE;
        return CurveError::OK;
    }

    case CurveOp::REFUND: {
        // A coin nobody has traded for CURVE_REFUND_IDLE_BLOCKS gives the money back.
        // Pro-rata: the holder's share of the tokens in circulation buys the same share
        // of the reserve. The curve empties to exactly zero when the last holder exits.
        if (spend_height - curve_height < CURVE_REFUND_IDLE_BLOCKS) return CurveError::NOT_IDLE;
        const int64_t sold = CurveTokensSold(reserve_in);
        if (sold <= 0 || intent.amount <= 0 || intent.amount > sold) return CurveError::BAD_AMOUNT;
        // out = reserve_in * tokens / sold   (128-bit, floor: dust stays in the reserve)
        const int64_t out = (int64_t)(((u128)reserve_in * (u128)intent.amount) / (u128)sold);
        const int64_t expected_reserve = reserve_in - out;
        // The last holder out closes the curve: an empty (or dust) reserve carries no output,
        // otherwise the final refund would be impossible and the money would be stranded.
        // THE LAW: the user never loses money — not even the last one.
        if (expected_reserve <= CURVE_DUST_SAT) {
            if (reserve_out != 0) return CurveError::BAD_AMOUNT;       // must not recreate the curve
            if (burned < expected_reserve) return CurveError::BAD_FEE; // the dust is burned, not pocketed
            return CurveError::OK;
        }
        if (reserve_out != expected_reserve) return CurveError::BAD_AMOUNT;
        // No fee on a refund. It is the user's own money coming home. THE LAW.
        return CurveError::OK;
    }

    case CurveOp::GRADUATE:
        // Reserved for the AMM transition (separate rule, separate UTXO shape).
        return CurveError::BAD_OUTPUT;
    }

    return CurveError::NO_INTENT;
}

} // namespace gbx
