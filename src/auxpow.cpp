// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2014-2024 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxpow.h>

#include <hash.h>
#include <pow.h>
#include <primitives/block.h>
#include <script/script.h>
#include <streams.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <algorithm>

uint256 CPureBlockHeader::GetHash() const
{
    return (HashWriter{} << *this).GetHash();
}

namespace {

/**
 * Walk a merkle branch upward from a leaf hash, returning the implied root.
 * nIndex encodes the leaf's position; each bit picks left/right at that level.
 */
uint256 CheckMerkleBranch(uint256 hash,
                          const std::vector<uint256>& vMerkleBranch,
                          int nIndex)
{
    if (nIndex < 0) return uint256();
    for (const uint256& otherside : vMerkleBranch) {
        if (nIndex & 1) {
            hash = Hash(otherside, hash);
        } else {
            hash = Hash(hash, otherside);
        }
        nIndex >>= 1;
    }
    return hash;
}

} // anonymous namespace

bool CAuxPow::check(const uint256& hashAuxBlock, int32_t nChainId,
                    const Consensus::Params& /*params*/) const
{
    // 1) The aux merkle branch index must be sane (no negative).
    if (nIndex != 0) {
        // Per the merged-mining spec the parent coinbase must be the first
        // transaction in the parent block, so its index is 0.
        return false;
    }

    // 2) The coinbase tx must actually be present.
    if (!coinbaseTx) return false;

    // 3) The coinbase must connect to the parent block's merkle root.
    const uint256 nRootFromCoinbase =
        CheckMerkleBranch(coinbaseTx->GetHash().ToUint256(), vMerkleBranch, nIndex);
    if (nRootFromCoinbase != parentBlock.hashMerkleRoot) {
        return false;
    }

    // 4) Compute the expected commitment root that the parent coinbase must
    //    carry: the GBX block hash, lifted through the chain merkle branch.
    const uint256 nRootHash =
        CheckMerkleBranch(hashAuxBlock, vChainMerkleBranch, nChainIndex);
    // Root hash is serialized in reverse byte order in the coinbase script.
    std::vector<unsigned char> vchRootHash(nRootHash.begin(), nRootHash.end());
    std::reverse(vchRootHash.begin(), vchRootHash.end());

    // 5) Find the merged-mining magic header in the coinbase scriptSig.
    static const unsigned char pchMergedMiningHeader[] = {0xfa, 0xbe, 'm', 'm'};
    const CScript script = coinbaseTx->vin[0].scriptSig;
    CScript::const_iterator pcHead =
        std::search(script.begin(), script.end(),
                    pchMergedMiningHeader,
                    pchMergedMiningHeader + sizeof(pchMergedMiningHeader));
    CScript::const_iterator pc =
        std::search(script.begin(), script.end(),
                    vchRootHash.begin(), vchRootHash.end());
    if (pc == script.end()) {
        return false; // commitment to the GBX block not found at all
    }

    if (pcHead != script.end()) {
        // With the magic header present, it must appear exactly once and the
        // root hash must immediately follow it (anti-stuffing).
        if (script.end() != std::search(pcHead + 1, script.end(),
                                        pchMergedMiningHeader,
                                        pchMergedMiningHeader + sizeof(pchMergedMiningHeader))) {
            return false; // duplicate magic
        }
        if (pc != pcHead + sizeof(pchMergedMiningHeader)) {
            return false; // root hash must follow the magic directly
        }
    } else {
        // Without the magic header, the commitment must be in the first
        // bytes of the coinbase script (no room to inject a second tree).
        if (pc - script.begin() > 20) {
            return false;
        }
    }

    // 6) After the root hash: 4-byte merkle tree size + 4-byte nonce.
    pc += vchRootHash.size();
    if (script.end() - pc < 8) {
        return false;
    }

    const uint32_t nSize = ReadLE32(&pc[0]);
    const unsigned int merkleHeight = vChainMerkleBranch.size();
    if (nSize != (1u << merkleHeight)) {
        return false; // tree size must match branch length
    }

    const uint32_t nNonce = ReadLE32(&pc[4]);
    // The chain index must be the deterministic slot for our chain id, so a
    // single parent coinbase cannot be replayed to forge a different chain.
    const uint32_t rand =
        nNonce * 1103515245u + 12345u + static_cast<uint32_t>(nChainId);
    const uint32_t expectedIndex =
        (rand * 1103515245u + 12345u) % (1u << merkleHeight);
    if (static_cast<uint32_t>(nChainIndex) != expectedIndex) {
        return false;
    }

    return true;
}

bool CheckAuxPowProofOfWork(const CBlockHeader& block, const Consensus::Params& params)
{
    if (!block.IsAuxPow()) {
        // Legacy header must not carry an auxpow object.
        if (block.auxpow) return false;
        return CheckProofOfWork(block.GetHash(), block.nBits, params);
    }
    // AuxPow header: proof must be attached, must commit to this exact
    // header, and the parent block's hash must satisfy the GBX target.
    if (!block.auxpow) return false;
    if (!block.auxpow->check(block.GetHash(), params.nAuxPowChainId, params)) {
        return false;
    }
    return CheckProofOfWork(block.auxpow->getParentBlockPoWHash(), block.nBits, params);
}
