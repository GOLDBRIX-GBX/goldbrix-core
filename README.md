<p align="center">
</p>

<h1 align="center">GoldBrix (GBX)</h1>

<p align="center">
  An autonomous, ownerless Proof-of-Work network with a built-in launchpad for community tokens.<br>
  No company. No CEO. No admin key. The rules live in code and consensus.
</p>

<p align="center">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue.svg">
  <img alt="Consensus" src="https://img.shields.io/badge/consensus-PoW%20SHA--256d-orange.svg">
  <img alt="Max supply" src="https://img.shields.io/badge/max%20supply-15%2C000%2C000%20GBX-yellow.svg">
</p>

---

## Core properties
- Proof-of-Work (SHA-256d). Capped supply: 15,000,000 GBX.
- Dual-phase emission: a bootstrap phase issues 50 GBX per 10-minute block up
  to block 20,000, then a production phase with 3-second blocks (LWMA-3
  difficulty) and a 0.25 GBX subsidy, halving every 28,000,000 blocks.
- Fair launch: open, permissionless Proof-of-Work since genesis — anyone can
  mine. No premine, no presale. Coins exist only by mining and by buying
  through the app/web, and all balances are transparent and verifiable on-chain.
- Anti-reorg finality: deep chain reorganizations beyond a fixed depth are
  rejected by consensus, hardening the network against 51% attacks.
- Token launchpad: create community tokens with a bonding curve and automatic
  graduation to an on-chain AMM with all-burn fees.

## Design principles
- Autonomous: built to run without an operator; services self-heal.
- Ownerless and anonymous: the network belongs only to those who mine and hold.
- No human key (endgame): economic rules and treasury locks move into
  consensus so no single key can change supply, fees, or drain funds.
- Code is law: supply, fees and graduation are fixed in code.

## Build and verification
GoldBrix Core builds with CMake. Reproducible builds are produced with GNU Guix
(see `contrib/guix`), and published binary checksums (`SHA256SUMS`) accompany
each release, so anyone can independently rebuild and verify that the running
binary matches this source.

## License
Released under the MIT License. See [COPYING](COPYING).
