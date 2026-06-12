// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Copyright (c) 2014-2024 Daniel Kraft
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AUXPOW_H
#define BITCOIN_AUXPOW_H

#include <consensus/params.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <vector>

class CBlockHeader;
class CBlock;

/**
 * Merged-mining (auxiliary proof-of-work) support for GoldBrix.
 *
 * A GBX block can be mined as an auxiliary chain of an external SHA-256d
 * parent chain. The parent chain's miner embeds a commitment to the GBX
 * block hash inside the parent coinbase transaction. The CAuxPow structure
 * carries everything a GBX node needs to verify, independently, that the
 * parent block did enough proof-of-work and that it genuinely committed to
 * this exact GBX block.
 *
 * The parent chain is treated generically: GBX never depends on the identity
 * of the parent, only on it being a valid SHA-256d header that meets the GBX
 * target and commits to the GBX hash.
 */

/** Marks a block as carrying auxiliary proof-of-work (flag in nVersion). */
static const int32_t VERSION_AUXPOW = (1 << 8); // 0x100

/** Bits used to encode the GBX chain id in the high bits of nVersion. */
static const int32_t VERSION_CHAIN_START = (1 << 16);

/** The merged-mining chain id reserved for GoldBrix ("GB" = 0x4742). */
static const int32_t AUXPOW_CHAIN_ID = 0x4742;

/** A minimal pure SHA-256d block header for the parent chain.
 *  Identical 80-byte layout to a GBX header; no auxpow attached. */
class CPureBlockHeader
{
public:
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CPureBlockHeader() { SetNull(); }

    SERIALIZE_METHODS(CPureBlockHeader, obj) {
        READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot,
                  obj.nTime, obj.nBits, obj.nNonce);
    }

    void SetNull()
    {
        nVersion = 0;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    uint256 GetHash() const;
};

/**
 * Auxiliary proof-of-work: the data proving a GBX block was merge-mined
 * under a SHA-256d parent block.
 */
class CAuxPow
{
public:
    /** The parent block's coinbase transaction (carries the GBX commitment). */
    CTransactionRef coinbaseTx;

    /** Merkle branch linking coinbaseTx to the parent block merkle root. */
    std::vector<uint256> vMerkleBranch;
    int nIndex{0};

    /** Merkle branch linking the GBX hash into the aux merkle tree (multi-chain). */
    std::vector<uint256> vChainMerkleBranch;
    int nChainIndex{0};

    /** The parent block header (the block that actually did the PoW). */
    CPureBlockHeader parentBlock;

    CAuxPow() = default;

    SERIALIZE_METHODS(CAuxPow, obj) {
        READWRITE(TX_WITH_WITNESS(obj.coinbaseTx),
                  obj.vMerkleBranch, obj.nIndex,
                  obj.vChainMerkleBranch, obj.nChainIndex,
                  obj.parentBlock);
    }

    /**
     * Verify this auxpow proves the given GBX block hash, for the given chain
     * id, under the consensus params. Does NOT check the parent PoW target
     * itself — the caller checks parentBlock.GetHash() against the GBX nBits.
     */
    bool check(const uint256& hashAuxBlock, int32_t nChainId,
               const Consensus::Params& params) const;

    /** Convenience: hash of the parent block that carries the PoW. */
    uint256 getParentBlockPoWHash() const { return parentBlock.GetHash(); }
};

/** Full proof-of-work check for a GBX header (legacy or merged-mined).
 *  Legacy header: normal PoW on its own hash. AuxPow header: the attached
 *  proof must commit to this header and the PARENT hash must meet nBits. */
bool CheckAuxPowProofOfWork(const CBlockHeader& block, const Consensus::Params& params);

/* ---- helpers on the (base) block version field ---- */

inline int32_t GetBaseVersion(int32_t ver) { return ver % VERSION_CHAIN_START; }
inline int32_t GetChainId(int32_t ver) { return ver / VERSION_CHAIN_START; }
inline bool IsAuxPowVersion(int32_t ver) { return (ver & VERSION_AUXPOW) != 0; }

#endif // BITCOIN_AUXPOW_H
