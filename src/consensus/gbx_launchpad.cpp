// Copyright (c) 2026 The GoldBrix developers
// Distributed under the MIT software license.

#include <consensus/gbx_launchpad.h>

#include <coins.h>
#include <consensus/params.h>
#include <consensus/validation.h>

#include <cstring>

namespace gbx {

//! Wire format of the intent, carried in a single OP_RETURN:
//!   "GBX:C:" (6 bytes) | op (1) | coin_id (32) | amount (8, big-endian) = 47 bytes
static const char* CURVE_TAG = "GBX:C:";
static constexpr size_t CURVE_TAG_LEN = 6;
static constexpr size_t CURVE_PAYLOAD_LEN = CURVE_TAG_LEN + 1 + 32 + 8 + 8 + 33; // 88

static bool IsKnownOp(unsigned char c)
{
    return c == (unsigned char)CurveOp::CREATE    || c == (unsigned char)CurveOp::BUY  ||
           c == (unsigned char)CurveOp::SELL      || c == (unsigned char)CurveOp::REFUND ||
           c == (unsigned char)CurveOp::GRADUATE  || c == (unsigned char)CurveOp::POOL_BUY ||
           c == (unsigned char)CurveOp::POOL_SELL;
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

        const unsigned char* p = data.data() + CURVE_TAG_LEN + 1 + 32;
        int64_t amount = 0, tokens_out = 0;
        for (int i = 0; i < 8; ++i) amount     = (amount     << 8) | (int64_t)p[i];       // big-endian
        for (int i = 0; i < 8; ++i) tokens_out = (tokens_out << 8) | (int64_t)p[8 + i];
        if (amount < 0 || tokens_out < 0) return std::nullopt;               // no negatives, ever
        intent.amount = amount;
        intent.tokens_out = tokens_out;
        intent.pubkey.assign(p + 16, p + 16 + 33);
        if (intent.pubkey[0] != 0x02 && intent.pubkey[0] != 0x03) return std::nullopt; // compressed only

        found = intent;
    }
    return found;
}


uint256 CurveIdFromOutpoint(const COutPoint& out)
{
    unsigned char buf[36];
    std::memcpy(buf, out.hash.begin(), 32);
    const uint32_t n = out.n;
    buf[32] = (unsigned char)((n >> 24) & 0xff);
    buf[33] = (unsigned char)((n >> 16) & 0xff);
    buf[34] = (unsigned char)((n >> 8) & 0xff);
    buf[35] = (unsigned char)(n & 0xff);
    uint256 id;
    CSHA256().Write(buf, sizeof(buf)).Finalize(id.begin());
    return id;
}

CScript PoolWitnessScript(const uint256& coin_id, int64_t tokens)
{
    std::vector<unsigned char> t(8);
    for (int i = 0; i < 8; ++i) t[i] = (unsigned char)((tokens >> ((7 - i) * 8)) & 0xff);
    CScript s;
    s << std::vector<unsigned char>(coin_id.begin(), coin_id.end());
    s << t;
    s << OP_2DROP << OP_TRUE;
    return s;
}

CScript PoolScriptPubKey(const uint256& coin_id, int64_t tokens)
{
    const CScript ws = PoolWitnessScript(coin_id, tokens);
    uint256 h;
    CSHA256().Write(ws.data(), ws.size()).Finalize(h.begin());
    CScript spk;
    spk << OP_0 << std::vector<unsigned char>(h.begin(), h.end());
    return spk;
}

std::optional<int64_t> ParsePoolWitnessScript(const std::vector<unsigned char>& ws_bytes, const uint256& coin_id)
{
    const CScript ws(ws_bytes.begin(), ws_bytes.end());
    CScript::const_iterator pc = ws.begin();
    opcodetype op;
    std::vector<unsigned char> id, t;
    if (!ws.GetOp(pc, op, id) || id.size() != 32) return std::nullopt;
    if (!ws.GetOp(pc, op, t)  || t.size()  != 8)  return std::nullopt;
    if (!ws.GetOp(pc, op) || op != OP_2DROP)      return std::nullopt;
    if (!ws.GetOp(pc, op) || op != OP_TRUE)       return std::nullopt;
    if (pc != ws.end())                           return std::nullopt;
    uint256 got; std::memcpy(got.begin(), id.data(), 32);
    if (got != coin_id) return std::nullopt;
    int64_t tokens = 0;
    for (int i = 0; i < 8; ++i) tokens = (tokens << 8) | (int64_t)t[i];
    if (tokens < 0) return std::nullopt;
    return tokens;
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

//! Is the declared token holding actually present in the outputs?
//! We rebuild the exact scriptPubKey from (coin_id, amount, pubkey) and look for it:
//! a lookalike script cannot pass, because the hash would differ.
static bool HasTokenOutput(const CTransaction& tx, const uint256& coin_id, int64_t amount,
                           const std::vector<unsigned char>& pubkey)
{
    if (amount <= 0 || pubkey.size() != 33) return false;
    const CScript spk = TokenScriptPubKey(coin_id, amount, pubkey);
    for (const CTxOut& out : tx.vout) {
        if (out.scriptPubKey == spk) return true;
    }
    return false;
}

CurveError CheckCurveTransition(const CTransaction& tx,
                                const CurveIntent& intent,
                                int64_t reserve_in,
                                int curve_height,
                                int spend_height,
                                int refund_idle_blocks)
{
    if (reserve_in < 0) return CurveError::BAD_AMOUNT;

    const int64_t reserve_out = CurveOutputValue(tx, intent.coin_id);
    const int64_t burned = BurnedAmount(tx);

    switch (intent.op) {

    case CurveOp::CREATE: {
        // A new curve is born. There is nothing to spend yet, so reserve_in must be zero:
        // the transaction funds the very first output from ordinary money.
        // intent.amount = the creator's first buy (gross). No launch fee exists — the only
        // cost of creating a coin is taking a real position in it, priced like anyone else's.
        if (reserve_in != 0) return CurveError::BAD_AMOUNT;
        if (intent.amount < CURVE_MIN_DEV_BUY_SAT) return CurveError::BAD_AMOUNT;
        // The id must be the fingerprint of this transaction's own first input. Nobody can
        // create a coin whose id they did not earn, and no id can ever be created twice.
        if (tx.vin.empty()) return CurveError::BAD_OUTPUT;
        if (intent.coin_id != CurveIdFromOutpoint(tx.vin[0].prevout)) return CurveError::BAD_COIN_ID;
        const int64_t fee = CurveFee(intent.amount);
        const int64_t net = intent.amount - fee;
        int64_t tokens_out = 0, expected_reserve = 0;
        if (!CurveBuy(0, net, tokens_out, expected_reserve)) return CurveError::BAD_AMOUNT;
        if (reserve_out != expected_reserve) return CurveError::BAD_AMOUNT;
        if (burned < fee) return CurveError::BAD_FEE;
        if (intent.tokens_out != tokens_out) return CurveError::BAD_TOKENS;
        if (!HasTokenOutput(tx, intent.coin_id, tokens_out, intent.pubkey)) return CurveError::BAD_TOKENS;
        if (TokensInInputs(tx, intent.coin_id) != 0) return CurveError::BAD_TOKENS;
        return CurveError::OK;
    }

    case CurveOp::BUY: {
        // intent.amount = gross GBX the buyer commits (sat).
        const int64_t fee = CurveFee(intent.amount);
        const int64_t net = intent.amount - fee;
        int64_t tokens_out = 0, expected_reserve = 0;
        if (!CurveBuy(reserve_in, net, tokens_out, expected_reserve)) return CurveError::CURVE_EXHAUSTED;
        if (expected_reserve <= 0) return CurveError::BAD_AMOUNT;      // a buy always leaves money behind
        if (reserve_out != expected_reserve) return CurveError::BAD_AMOUNT;
        if (burned < fee) return CurveError::BAD_FEE;   // the fee is burned, never collected
        // Tokens cannot be conjured: the buyer receives EXACTLY what the curve says, no more.
        if (intent.tokens_out != tokens_out) return CurveError::BAD_TOKENS;
        if (!HasTokenOutput(tx, intent.coin_id, tokens_out, intent.pubkey)) return CurveError::BAD_TOKENS;
        // A buy destroys no tokens.
        if (TokensInInputs(tx, intent.coin_id) != 0) return CurveError::BAD_TOKENS;
        return CurveError::OK;
    }

    case CurveOp::SELL: {
        // intent.amount = tokens burned back into the curve.
        int64_t gross = 0, expected_reserve = 0;
        if (!CurveSell(reserve_in, intent.amount, gross, expected_reserve)) return CurveError::BAD_AMOUNT;
        if (reserve_out != expected_reserve) return CurveError::BAD_AMOUNT;
        const int64_t fee = CurveFee(gross);
        if (burned < fee) return CurveError::BAD_FEE;
        // You can only sell what you can prove you hold: the spent token scripts carry the amount,
        // and spending them required your signature. Nothing else counts.
        const int64_t held = TokensInInputs(tx, intent.coin_id);
        if (held < 0) return CurveError::BAD_TOKENS;
        if (held < intent.amount) return CurveError::BAD_TOKENS;
        // Whatever is left over must come back as a new holding, or it is destroyed.
        const int64_t change = held - intent.amount;
        if (intent.tokens_out != change) return CurveError::BAD_TOKENS;
        if (change > 0 && !HasTokenOutput(tx, intent.coin_id, change, intent.pubkey)) return CurveError::BAD_TOKENS;
        return CurveError::OK;
    }

    case CurveOp::REFUND: {
        // A coin nobody has traded for CURVE_REFUND_IDLE_BLOCKS gives the money back.
        // Pro-rata: the holder's share of the tokens in circulation buys the same share
        // of the reserve. The curve empties to exactly zero when the last holder exits.
        if (spend_height - curve_height < refund_idle_blocks) return CurveError::NOT_IDLE;
        const int64_t sold = CurveTokensSold(reserve_in);
        if (sold <= 0 || intent.amount <= 0 || intent.amount > sold) return CurveError::BAD_AMOUNT;
        const int64_t held_r = TokensInInputs(tx, intent.coin_id);
        if (held_r < 0 || held_r < intent.amount) return CurveError::BAD_TOKENS;   // prove it is yours
        const int64_t change_r = held_r - intent.amount;
        if (intent.tokens_out != change_r) return CurveError::BAD_TOKENS;
        if (change_r > 0 && !HasTokenOutput(tx, intent.coin_id, change_r, intent.pubkey)) return CurveError::BAD_TOKENS;
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

    case CurveOp::GRADUATE: {
        // Anyone may trigger it — it is not a privilege, it is a fact about the reserve.
        if (reserve_in < CURVE_GRADUATION_SAT) return CurveError::NOT_GRADUATED;
        // The curve must not survive: no curve output may be recreated.
        if (reserve_out != 0) return CurveError::BAD_OUTPUT;
        // The whole reserve moves into the pool, together with the tokens that were never
        // sold on the curve. Nothing is skimmed on the way: liquidity in = liquidity out.
        const int64_t sold = CurveTokensSold(reserve_in);
        const int64_t pool_tokens = CURVE_LP_TOKENS + (CURVE_TOKENS - sold);
        const CScript pool_spk = PoolScriptPubKey(intent.coin_id, pool_tokens);
        int64_t pool_value = -1;
        for (const CTxOut& out : tx.vout) {
            if (out.scriptPubKey != pool_spk) continue;
            if (pool_value >= 0) return CurveError::BAD_OUTPUT;   // two pools = malformed
            pool_value = out.nValue;
        }
        if (pool_value != reserve_in) return CurveError::BAD_AMOUNT;  // every unit follows
        // Graduation burns nothing and mints nothing: it only changes the shape of the box.
        if (TokensInInputs(tx, intent.coin_id) != 0) return CurveError::BAD_TOKENS;
        return CurveError::OK;
    }
    }

    return CurveError::NO_INTENT;
}


CurveError CheckPoolTransition(const CTransaction& tx,
                              const CurveIntent& intent,
                              int64_t pool_gbx_in,
                              int64_t pool_tok_in)
{
    if (pool_gbx_in <= 0 || pool_tok_in <= 0) return CurveError::BAD_AMOUNT;

    const int64_t burned = BurnedAmount(tx);
    int64_t new_gbx = 0, new_tok = 0;

    if (intent.op == CurveOp::POOL_BUY) {
        // intent.amount = gross GBX the buyer commits.
        const int64_t fee = PoolFee(intent.amount);
        const int64_t net = intent.amount - fee;
        int64_t tokens_out = 0;
        if (!PoolBuy(pool_gbx_in, pool_tok_in, net, tokens_out, new_gbx, new_tok)) return CurveError::BAD_AMOUNT;
        if (burned < fee) return CurveError::BAD_FEE;
        if (intent.tokens_out != tokens_out) return CurveError::BAD_TOKENS;
        if (!HasTokenOutput(tx, intent.coin_id, tokens_out, intent.pubkey)) return CurveError::BAD_TOKENS;
        if (TokensInInputs(tx, intent.coin_id) != 0) return CurveError::BAD_TOKENS;
    } else if (intent.op == CurveOp::POOL_SELL) {
        // intent.amount = tokens sold into the pool.
        int64_t gross = 0;
        if (!PoolSell(pool_gbx_in, pool_tok_in, intent.amount, gross, new_gbx, new_tok)) return CurveError::BAD_AMOUNT;
        const int64_t fee = PoolFee(gross);
        if (burned < fee) return CurveError::BAD_FEE;
        const int64_t held = TokensInInputs(tx, intent.coin_id);
        if (held < 0 || held < intent.amount) return CurveError::BAD_TOKENS;
        const int64_t change = held - intent.amount;
        if (intent.tokens_out != change) return CurveError::BAD_TOKENS;
        if (change > 0 && !HasTokenOutput(tx, intent.coin_id, change, intent.pubkey)) return CurveError::BAD_TOKENS;
    } else {
        return CurveError::NO_INTENT;
    }

    // The pool must be recreated with EXACTLY the reserves the product dictates.
    // No skimming, no draining, no owner. The liquidity outlives everyone.
    const CScript spk = PoolScriptPubKey(intent.coin_id, new_tok);
    int64_t found = -1;
    for (const CTxOut& out : tx.vout) {
        if (out.scriptPubKey != spk) continue;
        if (found >= 0) return CurveError::BAD_OUTPUT;
        found = out.nValue;
    }
    if (found != new_gbx) return CurveError::BAD_AMOUNT;
    return CurveError::OK;
}

bool CheckCurveInputs(const CTransaction& tx,
                      TxValidationState& state,
                      const CCoinsViewCache& inputs,
                      int nSpendHeight,
                      const Consensus::Params& params)
{
    if (params.nLaunchpadHeight <= 0 || nSpendHeight < params.nLaunchpadHeight) return true; // not active yet

    // Does this transaction spend a curve reserve? We only know once we have the intent:
    // the coin_id in the OP_RETURN lets us rebuild the canonical scriptPubKey and compare.
    const std::optional<CurveIntent> intent = ParseCurveIntent(tx);

    // Find any input paying to a curve script. Without an intent we cannot know which coin
    // it belongs to, so a curve UTXO can only be spent by a transaction that declares itself.
    int64_t reserve_in = -1;
    int curve_height = 0;
    int curve_inputs = 0;

    for (const CTxIn& in : tx.vin) {
        const Coin& coin = inputs.AccessCoin(in.prevout);
        if (coin.IsSpent()) continue;
        if (!intent.has_value()) continue;
        if (coin.out.scriptPubKey != CurveScriptPubKey(intent->coin_id)) continue;
        ++curve_inputs;
        reserve_in = coin.out.nValue;
        curve_height = coin.nHeight;
    }

    // Is a pool being spent? Its script is revealed in the witness, carrying the token side.
    if (intent.has_value() && (intent->op == CurveOp::POOL_BUY || intent->op == CurveOp::POOL_SELL)) {
        for (size_t i = 0; i < tx.vin.size(); ++i) {
            const CScriptWitness& wit = tx.vin[i].scriptWitness;
            if (wit.stack.empty()) continue;
            const std::optional<int64_t> pool_tok = ParsePoolWitnessScript(wit.stack.back(), intent->coin_id);
            if (!pool_tok.has_value()) continue;
            const Coin& coin = inputs.AccessCoin(tx.vin[i].prevout);
            if (coin.IsSpent()) continue;
            if (coin.out.scriptPubKey != PoolScriptPubKey(intent->coin_id, *pool_tok)) continue;
            const CurveError perr = CheckPoolTransition(tx, *intent, coin.out.nValue, *pool_tok);
            if (perr == CurveError::OK) return true;
            return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-pool-invalid");
        }
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-pool-not-spent");
    }

    if (curve_inputs == 0) {
        // Nothing spent — but a transaction that CREATES a curve out of ordinary money must
        // still obey the rules, or a coin could be born with tokens nobody paid for.
        if (!intent.has_value()) return true;
        if (intent->op != CurveOp::CREATE) {
            // Declaring an op without spending the curve it names is meaningless; but it is
            // also harmless (no reserve moves), so let it through rather than invent a rule.
            return true;
        }
        const CurveError cerr = CheckCurveTransition(tx, *intent, /*reserve_in=*/0,
                                                     /*curve_height=*/nSpendHeight, nSpendHeight,
                                                     params.nCurveRefundIdleBlocks);
        if (cerr == CurveError::OK) return true;
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-bad-create");
    }
    if (curve_inputs > 0 && intent.has_value() && intent->op == CurveOp::CREATE) {
        // You cannot "create" a curve that already exists.
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-already-exists");
    }
    if (curve_inputs > 1) {
        return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-multiple-inputs");
    }

    const CurveError err = CheckCurveTransition(tx, *intent, reserve_in, curve_height, nSpendHeight, params.nCurveRefundIdleBlocks);
    switch (err) {
    case CurveError::OK:               return true;
    case CurveError::BAD_AMOUNT:       return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-bad-amount");
    case CurveError::BAD_FEE:          return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-fee-not-burned");
    case CurveError::BAD_OUTPUT:       return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-bad-output");
    case CurveError::NOT_IDLE:         return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-not-idle");
    case CurveError::CURVE_EXHAUSTED:  return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-exhausted");
    case CurveError::MULTIPLE_CURVES:  return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-multiple-inputs");
    case CurveError::BAD_TOKENS:       return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-bad-tokens");
    case CurveError::BAD_COIN_ID:      return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-bad-coin-id");
    case CurveError::NOT_GRADUATED:    return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-not-graduated");
    case CurveError::NO_INTENT:        return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-no-intent");
    }
    return state.Invalid(TxValidationResult::TX_CONSENSUS, "gbx-curve-unknown");
}

} // namespace gbx
