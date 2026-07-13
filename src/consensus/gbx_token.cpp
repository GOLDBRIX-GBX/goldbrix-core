// Copyright (c) 2026 The GoldBrix developers
// Distributed under the MIT software license.

#include <consensus/gbx_token.h>

namespace gbx {

int64_t TokensInOutputs(const CTransaction& tx, const uint256& coin_id)
{
    // We cannot invert P2WSH, so we cannot "read" an output's token amount directly.
    // Instead the transaction must declare it (in the intent) and we rebuild the exact
    // scriptPubKey to prove the declaration matches what is on the chain. That check
    // lives in CheckCurveTransition, which knows both the amount and the pubkey.
    // Here we only count outputs that carry SOME token script of this coin, which is
    // impossible without the amount — so this helper exists for symmetry and returns 0.
    (void)tx; (void)coin_id;
    return 0;
}

int64_t TokensInInputs(const CTransaction& tx, const uint256& coin_id)
{
    // A P2WSH spend reveals its witness script: the last witness stack item.
    // The amount is inside it, so the node reads the holding straight from the coin
    // being spent. No parent lookup, no index, no state.
    int64_t total = 0;
    for (size_t i = 0; i < tx.vin.size(); ++i) {
        const CScriptWitness& wit = tx.vin[i].scriptWitness;
        if (wit.stack.empty()) continue;
        const std::optional<TokenOut> t = ParseTokenWitnessScript(wit.stack.back());
        if (!t.has_value()) continue;
        if (t->coin_id != coin_id) continue;
        if (total > std::numeric_limits<int64_t>::max() - t->amount) return -1; // overflow: reject
        total += t->amount;
    }
    return total;
}

} // namespace gbx
