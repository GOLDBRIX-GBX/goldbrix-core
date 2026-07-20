<p align="center">
  <img src="branding/goldbrix-logo.png" alt="GoldBrix" width="440">
</p>

<h1 align="center">GoldBrix (GBX)</h1>

<p align="center">
  <b>Real chain. Real burn. Real fairness.</b><br>
  An autonomous, ownerless Proof-of-Work network with a built-in token launchpad.<br>
  No company. No CEO. No admin key. The rules live in code and consensus.
</p>

<p align="center">
  <a href="contrib/guix"><img alt="Reproducible build" src="https://img.shields.io/badge/build-reproducible%20(Guix)-brightgreen.svg"></a>
  <a href="src/consensus"><img alt="Consensus" src="https://img.shields.io/badge/consensus-PoW%20SHA--256d-orange.svg"></a>
  <a href="src/chainparams.cpp"><img alt="Max supply" src="https://img.shields.io/badge/max%20supply-15%2C000%2C000%20GBX-yellow.svg"></a>
  <a href="https://explorer.goldbrix.app"><img alt="Fees" src="https://img.shields.io/badge/launchpad%20fees-100%25%20burned-red.svg"></a>
</p>

---

## Get GoldBrix

- **Web app:** [goldbrix.app](https://goldbrix.app) — create a wallet, buy/sell GBX (non-custodial)
- **Android APK:** [downloads](https://goldbrix.app/downloads/) or [GitHub Releases](https://github.com/GOLDBRIX-GBX/goldbrix-core/releases/latest) — verify SHA-256 against [version.json](https://goldbrix.app/version.json)
- **Explorer:** [explorer.goldbrix.app](https://explorer.goldbrix.app)
- **Mine (0 fee, non-custodial):** `stratum+tcp://goldbrix.app:3333` — [guide](https://github.com/GOLDBRIX-GBX/goldbrix-tools/blob/main/docs/MINING.md) · live stats: [/pool-info](https://goldbrix.app/pool-info)
- **Run a node:** [guide](https://goldbrix.app/run-node) · release checksums: [SHA256SUMS](https://goldbrix.app/downloads/SHA256SUMS-v31-gbx-launchpad.txt) · binaries also on [GitHub Releases](https://github.com/GOLDBRIX-GBX/goldbrix-core/releases/latest)

## Why GoldBrix

Almost every token launch ends the same way: insiders pump, outsiders buy, then
the team pulls the liquidity and walks. GoldBrix is built so that cannot happen —
not because anyone promises it, but because **no one holds the keys to do it.**

There is no founder allocation, no presale, no admin switch. The network belongs
to the people who mine it and hold it. Honesty is enforced by code, not by trust.

## How it works, end to end

**On your phone or in any browser — no account, no permission, no signup:**

1. **Create a wallet.** 12 words, generated and stored on your device only. No server ever holds them.
2. **Forge a coin through work.** Your device mines a small, real proof-of-work (~1–3 minutes of hashing), then makes a first buy with a tiny minimum. That is the entire barrier: work, not money. The consensus itself verifies the proof — no gatekeeper can refuse you.
3. **Trade on bonding curves.** Buys and sells run against an automatic x·y=k curve enforced by consensus (`src/consensus/`). Nobody sets the price; large trades move it progressively. A refund on an idle curve returns the full amount, with no fee — this rule lives in consensus, not in a promise.
4. **Watch every fee burn.** Each launchpad action burns GBX to a provably unspendable address — anyone can sum the burns on the [explorer](https://explorer.goldbrix.app).
5. **Graduation by depth, not hype.** When a coin's reserve reaches a threshold derived from its own trading activity, it graduates to an AMM with protocol-owned, code-locked liquidity.

**On the network side — every role is open, none is required:**

- **Mine GBX** with any SHA-256d miner, solo non-custodial: the coinbase pays your address directly, pool fee 0 — verify live at [/pool-info](https://goldbrix.app/pool-info), [mining guide](https://github.com/GOLDBRIX-GBX/goldbrix-tools/blob/main/docs/MINING.md).
- **Run a node** with [one command](https://goldbrix.app/run-node); it serves a public read endpoint the wallets can use.
- **Provide liquidity** for atomic GBX↔USDC swaps (HTLC): either both legs settle or both refund — [lp-box source](https://github.com/GOLDBRIX-GBX/goldbrix-tools/tree/main/lp-box).

## Built for everyone — the same rules at every size

Most markets quietly favor whoever arrives with the most money. GoldBrix applies
one rulebook, in code:

- **The same rule at every size.** The curve and the AMM both price by x·y=k on
  real reserves: price impact grows with trade size — for everyone, identically.
  No volume discounts, no private allocations, no early tiers, no special
  treatment in any direction. A 10 GBX trade and a 100,000 GBX trade go through
  the exact same formula.
- **Pump-and-dump has no exit door.** Sell-side rate limits and per-address
  volume caps scale with real liquidity ([lp-box](https://github.com/GOLDBRIX-GBX/goldbrix-tools/tree/main/lp-box)).
  Graduation cannot be bought — it is derived from a coin's own sustained
  activity, not from a spike.
- **Nobody eats the small fish.** There are no insider allocations to dump on
  late buyers, no team wallet, no fee stream feeding an operator. Every fee
  burns; every participant — first or last, large or small — faces the same
  curve, the same rules, the same code.
- **Entry costs work, which everyone has.** Creating a coin takes minutes of
  proof-of-work on your own phone — the one resource that cannot be bought
  cheaper in bulk.

None of this is a pledge. Each rule above is consensus or reviewable code —
read it, rebuild it ([contrib/guix](contrib/guix), [checksums](https://goldbrix.app/downloads/SHA256SUMS-v31-gbx-launchpad.txt)), verify it.

## What makes it different

| | Typical project | GoldBrix |
|---|---|---|
| Ownership | Team / company | **No owner, no CEO** |
| Admin key | Yes (can mint, pause, drain) | **None** |
| Launch | Premine / presale to insiders | **Fair PoW from genesis** |
| Launchpad fees | Paid to the team | **100% burned** — L1 transaction fees go to miners, like any PoW chain |
| Rug pull | Team removes liquidity | **Liquidity protocol-owned, locked by code — no withdraw function** |
| Survival | Dies if team leaves | **Runs autonomously, self-heals** |

## Core properties

- **Proof-of-Work (SHA-256d)** with a hard cap of **15,000,000 GBX**.
- **Dual-phase emission:** a bootstrap phase of 50 GBX per 10-minute block up to
  block 20,000, then a production phase with 3-second blocks (LWMA-3 difficulty)
  and a 0.25 GBX subsidy, halving every 28,000,000 blocks.
- **Fair launch:** open, permissionless mining since genesis — anyone can mine.
  No premine, no presale. Coins exist only by mining or by buying through the
  app/web, and every balance is transparent and verifiable on-chain.
- **Anti-51% finality:** chain reorganizations beyond a fixed depth are rejected
  by consensus, hardening the network against majority attacks — with no trusted
  signer.

## The token launchpad

Anyone can create a community token. The mechanics are designed to be fair and
spam-resistant by construction:

- **Creating a token costs work, not money.** The creator mines a small proof-of-work on their own device, then makes the first buy, with a
  small minimum, to prevent spam and bot-flooding.
- **Every fee is burned — 100%.** Create, buy, sell, promote: each action burns
  GBX to a provably unspendable address. No fee ever reaches a person.
- **Burn-driven scarcity:** every trade permanently reduces supply, so activity
  compounds value for all holders instead of paying an operator.
- **Automatic graduation:** once a token reaches its bonding-curve threshold it
  graduates to an AMM. Its liquidity is **protocol-owned and locked by code** —
  no withdraw function exists, and every fee it earns is burned. At handover,
  pool custody moves into keyless on-chain constructions (see The endgame),
  making the lock permanent and verifiable by anyone.

> Your fees aren't someone's salary. Your activity burns supply.

## Autonomous by design

- **Runs without an operator.** Services self-heal and restart automatically.
- **No single point of failure.** Redundant nodes and mining.
- **Treasury protected by code:** circuit breakers, hard per-transaction and
  daily limits, and automatic stop on anomaly — defending against whales and
  dumps without any human intervention.
- **Idempotent by construction:** payments never double-send and never get lost.

The test for every feature: *does it keep running if the founder disappears
tomorrow?* If not, it is not ready.

## A federation, not a server

There is no central list of infrastructure. Nodes and liquidity providers announce
themselves **on-chain** (`GBX:NODE`, `GBX:LP` records), and every wallet discovers
them from the chain itself, cross-checking answers across multiple nodes
([node-registry source](https://github.com/GOLDBRIX-GBX/goldbrix-tools/tree/main/node-registry)).
When a node appears or disappears, nothing needs updating — the federation finds
itself. The founder's servers are just nodes among nodes; the network does not
know the difference.

## The endgame: no key

GoldBrix is designed to end without an owner. Economic rules — supply, fees,
graduation — are fixed in code. At handover, treasury and liquidity controls
move into keyless constructions and the founding keys are **destroyed, not
transferred**, so no person — including the creator — can ever change the supply,
redirect fees, or drain funds.

## The Founder

<p align="center">
  <img src="branding/gideon-brick.png" alt="Gideon Brick" width="180">
</p>

GoldBrix was laid down, brick by brick, by an anonymous builder known only as
**Gideon Brick** — who then stepped away. His absence is the design: a network
with no one to capture, pressure, or buy off. He mined as a
peer among peers, took no special share, and asked nothing in return. The name is
a marker, not a master.

> *"He laid the foundation, then walked away — and kept no key."*

## Build and verification

GoldBrix Core builds with CMake. Reproducible builds are produced with GNU Guix
(see `contrib/guix`), and published checksums (`SHA256SUMS`) accompany each
release — so anyone can independently rebuild from this source and verify the
running binary matches, byte for byte.

## License

Released under the MIT License. See [COPYING](COPYING).
