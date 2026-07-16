// Copyright (c) 2026 The GoldBrix developers
// Distributed under the MIT software license.
//
// GBX LAUNCHPAD IN CONSENSUS (IDEE V) — tokens as UTXOs.
//
// A memecoin balance is not a row in someone's database. It is an output on the chain,
// and it is yours because you hold the key — exactly like GBX itself.
//
// The amount and the coin it belongs to are carried INSIDE the script, so a validating
// node reads them straight from the coin being spent. No global state, no index, no
// lookup of the parent transaction. Ownership is a signature; supply is arithmetic.
//
//   witnessScript = <coin_id:32> <amount:8> OP_2DROP <pubkey:33> OP_CHECKSIG
//   scriptPubKey  = P2WSH(witnessScript)
#ifndef GBX_CONSENSUS_TOKEN_H
#define GBX_CONSENSUS_TOKEN_H

#include <crypto/sha256.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <cstring>
#include <optional>
#include <vector>

namespace gbx {

//! A token holding, as it lives on the chain.
struct TokenOut {
    uint256 coin_id;
    int64_t amount{0};
    std::vector<unsigned char> pubkey;   //!< 33 bytes, compressed
};

//! Build the canonical witness script for a token holding.
inline CScript TokenWitnessScript(const uint256& coin_id, int64_t amount, const std::vector<unsigned char>& pubkey)
{
    std::vector<unsigned char> amt(8);
    for (int i = 0; i < 8; ++i) amt[i] = (unsigned char)((amount >> ((7 - i) * 8)) & 0xff); // big-endian
    CScript s;
    s << std::vector<unsigned char>(coin_id.begin(), coin_id.end());
    s << amt;
    s << OP_2DROP;
    s << pubkey;
    s << OP_CHECKSIG;
    return s;
}

//! The scriptPubKey that actually appears in an output.
inline CScript TokenScriptPubKey(const uint256& coin_id, int64_t amount, const std::vector<unsigned char>& pubkey)
{
    const CScript ws = TokenWitnessScript(coin_id, amount, pubkey);
    uint256 h;
    CSHA256().Write(ws.data(), ws.size()).Finalize(h.begin());
    CScript spk;
    spk << OP_0 << std::vector<unsigned char>(h.begin(), h.end());
    return spk;
}

//! Read a token holding back out of a witness script (as revealed when it is spent).
//! Returns nothing if the script is not a canonical token script.
inline std::optional<TokenOut> ParseTokenWitnessScript(const std::vector<unsigned char>& ws_bytes)
{
    const CScript ws(ws_bytes.begin(), ws_bytes.end());
    CScript::const_iterator pc = ws.begin();
    opcodetype op;
    std::vector<unsigned char> id, amt, pk;

    if (!ws.GetOp(pc, op, id)  || id.size()  != 32) return std::nullopt;
    if (!ws.GetOp(pc, op, amt) || amt.size() != 8)  return std::nullopt;
    if (!ws.GetOp(pc, op) || op != OP_2DROP)        return std::nullopt;
    if (!ws.GetOp(pc, op, pk)  || pk.size()  != 33) return std::nullopt;
    if (!ws.GetOp(pc, op) || op != OP_CHECKSIG)     return std::nullopt;
    if (pc != ws.end())                             return std::nullopt;

    TokenOut t;
    std::memcpy(t.coin_id.begin(), id.data(), 32);
    int64_t amount = 0;
    for (int i = 0; i < 8; ++i) amount = (amount << 8) | (int64_t)amt[i];
    if (amount <= 0) return std::nullopt;            // a zero holding is not a holding
    t.amount = amount;
    t.pubkey = pk;
    return t;
}

//! Total tokens of one coin held by the outputs of a transaction.
//! Rebuilds each candidate scriptPubKey, so it cannot be fooled by a lookalike script.
int64_t TokensInOutputs(const CTransaction& tx, const uint256& coin_id);

//! Total tokens of one coin destroyed/moved by the inputs of a transaction.
//! The witness of a P2WSH spend reveals the script, which carries the amount.
int64_t TokensInInputs(const CTransaction& tx, const uint256& coin_id);

} // namespace gbx

#endif // GBX_CONSENSUS_TOKEN_H
