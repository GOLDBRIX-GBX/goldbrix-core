// Copyright (c) 2025 The GoldBrix developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxpow.h>

#include <chainparams.h>
#include <hash.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <streams.h>
#include <uint256.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(auxpow_tests, BasicTestingSetup)

namespace {

const unsigned char MM_MAGIC[4] = {0xfa, 0xbe, 'm', 'm'};

void put_le32(std::vector<unsigned char>& v, uint32_t x)
{
    v.push_back(static_cast<unsigned char>(x & 0xff));
    v.push_back(static_cast<unsigned char>((x >> 8) & 0xff));
    v.push_back(static_cast<unsigned char>((x >> 16) & 0xff));
    v.push_back(static_cast<unsigned char>((x >> 24) & 0xff));
}

uint256 SampleHash(uint32_t seed)
{
    return (HashWriter{} << seed).GetHash();
}

// Coinbase whose scriptSig carries the merged-mining commitment:
// [magic][reversed root][le32 tree-size][le32 nonce].
CTransactionRef MakeCommitCoinbase(const uint256& root, uint32_t treeSize, uint32_t nonce)
{
    std::vector<unsigned char> rev(root.begin(), root.end());
    std::reverse(rev.begin(), rev.end());

    std::vector<unsigned char> bytes;
    bytes.insert(bytes.end(), MM_MAGIC, MM_MAGIC + 4);
    bytes.insert(bytes.end(), rev.begin(), rev.end());
    put_le32(bytes, treeSize);
    put_le32(bytes, nonce);

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].scriptSig = CScript(bytes.begin(), bytes.end());
    mtx.vout.resize(1);
    return MakeTransactionRef(std::move(mtx));
}

// Single-chain (height 0) valid auxpow committing to hashAux.
CAuxPow MakeSingleChainAuxPow(const uint256& hashAux, uint32_t nonce = 7)
{
    CAuxPow aux;
    aux.vChainMerkleBranch.clear();
    aux.nChainIndex = 0;            // height 0 -> only slot
    aux.vMerkleBranch.clear();
    aux.nIndex = 0;
    aux.coinbaseTx = MakeCommitCoinbase(hashAux, 1u, nonce); // tree size 2^0 = 1
    aux.parentBlock.SetNull();
    aux.parentBlock.hashMerkleRoot = aux.coinbaseTx->GetHash().ToUint256();
    return aux;
}

} // namespace

BOOST_AUTO_TEST_CASE(version_field_encoding)
{
    const int32_t base = 4;
    const int32_t ver = (base | VERSION_AUXPOW) | (AUXPOW_CHAIN_ID * VERSION_CHAIN_START);
    BOOST_CHECK(IsAuxPowVersion(ver));
    BOOST_CHECK_EQUAL(GetChainId(ver), AUXPOW_CHAIN_ID);
    BOOST_CHECK_EQUAL(GetBaseVersion(ver), (base | VERSION_AUXPOW));
    BOOST_CHECK(!IsAuxPowVersion(base));
}

BOOST_AUTO_TEST_CASE(pureheader_serialize_roundtrip)
{
    CPureBlockHeader h;
    h.nVersion = 0x20000000;
    h.hashPrevBlock = SampleHash(1);
    h.hashMerkleRoot = SampleHash(2);
    h.nTime = 1718000000;
    h.nBits = 0x1d00ffff;
    h.nNonce = 12345;

    DataStream ss;
    ss << h;
    CPureBlockHeader h2;
    ss >> h2;

    BOOST_CHECK(h.GetHash() == h2.GetHash());
    BOOST_CHECK_EQUAL(h2.nNonce, 12345u);
    BOOST_CHECK_EQUAL(ss.size(), 0u);
}

BOOST_AUTO_TEST_CASE(auxpow_serialize_roundtrip)
{
    const uint256 hashAux = SampleHash(99);
    const CAuxPow aux = MakeSingleChainAuxPow(hashAux);

    DataStream ss;
    ss << aux;
    CAuxPow aux2;
    ss >> aux2;

    BOOST_CHECK_EQUAL(aux2.nIndex, 0);
    BOOST_CHECK_EQUAL(aux2.nChainIndex, 0);
    BOOST_CHECK(aux2.coinbaseTx != nullptr);
    BOOST_CHECK(aux.coinbaseTx->GetHash() == aux2.coinbaseTx->GetHash());
    BOOST_CHECK(aux.parentBlock.GetHash() == aux2.parentBlock.GetHash());
    BOOST_CHECK_EQUAL(ss.size(), 0u);
}

BOOST_AUTO_TEST_CASE(blockheader_conditional_serialize)
{
    const uint256 hashAux = SampleHash(5);

    // (a) auxpow flag set -> auxpow travels with the header
    CBlockHeader hdr;
    hdr.SetNull();
    hdr.nVersion = (4 | VERSION_AUXPOW) | (AUXPOW_CHAIN_ID * VERSION_CHAIN_START);
    hdr.auxpow = std::make_shared<CAuxPow>(MakeSingleChainAuxPow(hashAux));
    BOOST_CHECK(hdr.IsAuxPow());

    DataStream ss;
    ss << hdr;
    CBlockHeader hdr2;
    ss >> hdr2;
    BOOST_CHECK(hdr2.IsAuxPow());
    BOOST_CHECK(hdr2.auxpow != nullptr);
    BOOST_CHECK(hdr2.auxpow->coinbaseTx->GetHash() == hdr.auxpow->coinbaseTx->GetHash());
    BOOST_CHECK_EQUAL(ss.size(), 0u);

    // (b) no auxpow flag -> nothing extra serialized, auxpow stays null
    CBlockHeader legacy;
    legacy.SetNull();
    legacy.nVersion = 0x20000000;
    DataStream ls;
    ls << legacy;
    CBlockHeader legacy2;
    ls >> legacy2;
    BOOST_CHECK(!legacy2.IsAuxPow());
    BOOST_CHECK(legacy2.auxpow == nullptr);
    BOOST_CHECK_EQUAL(ls.size(), 0u);
}

BOOST_AUTO_TEST_CASE(check_accepts_valid_single_chain)
{
    const uint256 hashAux = SampleHash(42);
    const CAuxPow aux = MakeSingleChainAuxPow(hashAux);
    BOOST_CHECK(aux.check(hashAux, AUXPOW_CHAIN_ID, Params().GetConsensus()));
}

BOOST_AUTO_TEST_CASE(check_rejects_missing_coinbase)
{
    CAuxPow aux; // default: no coinbase
    BOOST_CHECK(!aux.check(SampleHash(42), AUXPOW_CHAIN_ID, Params().GetConsensus()));
}

BOOST_AUTO_TEST_CASE(check_rejects_nonzero_index)
{
    const uint256 hashAux = SampleHash(42);
    CAuxPow aux = MakeSingleChainAuxPow(hashAux);
    aux.nIndex = 1; // parent coinbase must be tx 0
    BOOST_CHECK(!aux.check(hashAux, AUXPOW_CHAIN_ID, Params().GetConsensus()));
}

BOOST_AUTO_TEST_CASE(check_rejects_wrong_commitment)
{
    const uint256 hashAux = SampleHash(42);
    const CAuxPow aux = MakeSingleChainAuxPow(hashAux);
    // Verify against a block hash that was never committed.
    BOOST_CHECK(!aux.check(SampleHash(43), AUXPOW_CHAIN_ID, Params().GetConsensus()));
}

BOOST_AUTO_TEST_SUITE_END()
