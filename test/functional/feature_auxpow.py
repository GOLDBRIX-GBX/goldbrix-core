#!/usr/bin/env python3
# Copyright (c) 2025 The GoldBrix developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test GoldBrix merged-mining (AuxPoW) consensus end-to-end on regtest.

Covers: a valid single-chain auxpow block is accepted; auxpow before the
activation height is rejected; legacy and auxpow blocks coexist (dual mode);
a corrupt auxpow blob is rejected at decode; an auxpow committing to the wrong
GBX block hash is rejected.
"""

import hashlib
import struct

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.messages import CTransaction, CTxIn, CTxOut, COutPoint
from test_framework.script import CScript

MM_MAGIC = b'\xfa\xbe' + b'mm'
ACTIVATION_HEIGHT = 10


def sha256d(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


def compact_to_target(nbits):
    exponent = nbits >> 24
    mantissa = nbits & 0x007fffff
    if exponent <= 3:
        return mantissa >> (8 * (3 - exponent))
    return mantissa << (8 * (exponent - 3))


def build_commit_coinbase(commit_be, tree_size=1, cb_nonce=0):
    """Coinbase whose scriptSig carries [magic][commit_be][le32 size][le32 nonce]."""
    script = MM_MAGIC + commit_be + struct.pack("<I", tree_size) + struct.pack("<I", cb_nonce)
    cb = CTransaction()
    cb.version = 1
    cb.vin = [CTxIn(COutPoint(0, 0xffffffff), CScript(script))]
    cb.vout = [CTxOut(0, CScript())]
    return cb


def build_auxpow_hex(commit_be, nbits, parent_prev=b'\x00' * 32):
    """Build a minimal single-chain CAuxPow serialization committing to commit_be.

    Layout matches the C++ CAuxPow SERIALIZE_METHODS:
      TX_WITH_WITNESS(coinbaseTx) | vMerkleBranch | nIndex |
      vChainMerkleBranch | nChainIndex | parentBlock(80 bytes).
    Empty merkle branches -> parent merkle root == coinbase txid.
    """
    cb = build_commit_coinbase(commit_be)
    txid_le = sha256d(cb.serialize_without_witness())  # 32 bytes LE = txid = parent merkle root

    target = compact_to_target(nbits)
    prefix = (struct.pack("<i", 0x20000000) + parent_prev + txid_le +
              struct.pack("<I", 1718000000) + struct.pack("<I", nbits))
    nonce = 0
    while True:
        header80 = prefix + struct.pack("<I", nonce)
        if int.from_bytes(sha256d(header80), 'little') <= target:
            break
        nonce += 1

    blob = b''
    blob += cb.serialize_with_witness()  # TX_WITH_WITNESS (no witness -> legacy)
    blob += b'\x00'                       # vMerkleBranch: empty vector
    blob += struct.pack("<i", 0)          # nIndex
    blob += b'\x00'                       # vChainMerkleBranch: empty vector
    blob += struct.pack("<i", 0)          # nChainIndex
    blob += header80                      # parentBlock
    return blob.hex()


class AuxPowTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        self.skip_if_no_wallet()

    def make_valid_auxpow(self, node, addr):
        """createauxblock -> build a valid auxpow committing to it. Returns (hash, hex)."""
        aux = node.createauxblock(addr)
        assert_equal(aux['chainid'], 0x4742)
        commit_be = bytes.fromhex(aux['hash'])  # RPC hex == big-endian commitment
        nbits = int(aux['bits'], 16)
        return aux['hash'], build_auxpow_hex(commit_be, nbits)

    def run_test(self):
        node = self.nodes[0]
        addr = node.getnewaddress()

        self.log.info("Mine legacy blocks up to just below activation")
        self.generatetoaddress(node, ACTIVATION_HEIGHT - 5, addr)  # height 5

        self.log.info("AuxPoW before activation height must be rejected")
        h_pre, ap_pre = self.make_valid_auxpow(node, addr)
        assert_equal(node.submitauxblock(h_pre, ap_pre), False)
        assert_equal(node.getblockcount(), ACTIVATION_HEIGHT - 5)

        self.log.info("Mine past activation height")
        self.generatetoaddress(node, 8, addr)  # height 13
        start = node.getblockcount()

        self.log.info("A valid auxpow block is accepted and extends the chain")
        h_ok, ap_ok = self.make_valid_auxpow(node, addr)
        assert_equal(node.submitauxblock(h_ok, ap_ok), True)
        assert_equal(node.getblockcount(), start + 1)
        auxpow_tip = node.getbestblockhash()

        self.log.info("Dual mode: a legacy block extends the auxpow tip")
        self.generatetoaddress(node, 1, addr)
        assert_equal(node.getblockcount(), start + 2)
        assert_equal(node.getblock(auxpow_tip)['confirmations'], 2)

        self.log.info("A second auxpow block stacks on top of the legacy block")
        h_ok2, ap_ok2 = self.make_valid_auxpow(node, addr)
        assert_equal(node.submitauxblock(h_ok2, ap_ok2), True)
        assert_equal(node.getblockcount(), start + 3)

        self.log.info("A corrupt auxpow blob is rejected at decode")
        h_bad = node.createauxblock(addr)['hash']
        assert_raises_rpc_error(-22, "AuxPow decode failed", node.submitauxblock, h_bad, "00")

        self.log.info("An auxpow committing to the wrong block hash is rejected")
        aux = node.createauxblock(addr)
        wrong = bytearray(bytes.fromhex(aux['hash']))
        wrong[0] ^= 0xff  # commit to a different hash
        ap_wrong = build_auxpow_hex(bytes(wrong), int(aux['bits'], 16))
        assert_equal(node.submitauxblock(aux['hash'], ap_wrong), False)

        self.log.info("AuxPoW consensus checks passed")


if __name__ == '__main__':
    AuxPowTest(__file__).main()
