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

} // namespace gbx
