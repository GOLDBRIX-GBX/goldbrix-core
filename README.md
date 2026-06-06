# GoldBrix (GBX)

GoldBrix is an autonomous, ownerless cryptocurrency network with a built-in
launchpad for community tokens. No company, no CEO, no admin key. The rules
live in code and consensus.

## Core properties
- Proof-of-Work (SHA-256d). Capped supply: 15,000,000 GBX.
- Dual-phase emission: a bootstrap phase issues 50 GBX per 10-minute block up
  to block 20,000, then a production phase with 3-second blocks (LWMA-3
  difficulty) and a 0.25 GBX subsidy, halving every 28,000,000 blocks.
- Fair launch: no premine, no founder allocation. Coins exist only by mining
  and by buying through the app/web.
- Token launchpad: create community tokens with a bonding curve and automatic
  graduation to an on-chain AMM with all-burn fees.

## Design principles
- Autonomous: built to run without an operator; services self-heal.
- Ownerless and anonymous: the network belongs only to those who mine and hold.
- No human key (endgame): economic rules and treasury locks move into
  consensus so no single key can change supply, fees, or drain funds.
- Code is law: supply, fees and graduation are fixed in code.

## Build and verification
Reproducible-build instructions and published binary checksums accompany the
public release, so anyone can verify the running binary matches this source.

## License
Released under the MIT License. See COPYING.
