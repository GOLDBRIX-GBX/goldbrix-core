# Mining GoldBrix

GoldBrix (GBX) is an autonomous, permissionless proof-of-work cryptocurrency.
There is no company, no CEO, and no central operator: the network runs on its
own, secured by the miners who participate in it. Anyone, anywhere, can run a
node and mine — there is no premine, no presale, and no allowlist. This guide
walks you through everything needed to join the network and start mining GBX.

## Network at a glance

| Property | Value |
|---|---|
| Proof of work | SHA-256d |
| Maximum supply | 15,000,000 GBX |
| Block reward | 50 GBX per block until block 20,000, then 0.25 GBX per block |
| Halving interval | every 28,000,000 blocks |
| Difficulty adjustment | per block (LWMA) |
| Default P2P port | 39444 |
| Default RPC port | 8332 |

Every economic rule above is enforced by the consensus code that each node
runs, and can be independently verified on-chain. No one can change supply,
rewards, or difficulty after launch.

## Requirements

To mine GoldBrix you need two things:

1. **A full node** — the `goldbrixd` daemon. It validates the blockchain,
   connects to peers, and exposes the RPC interface your miner talks to. A few
   gigabytes of free disk and a stable internet connection are enough to start.
2. **SHA-256d mining software** — any CPU, GPU, or ASIC miner that supports the
   standard `getblocktemplate` workflow. Because GoldBrix uses the same
   SHA-256d hashing as the most widely deployed mining hardware, existing
   SHA-256d miners work without modification.

## 1. Get the software

Download the latest `goldbrixd` and `goldbrix-cli` binaries from the releases
page, or build them yourself from source:

- Build from source: see [build-unix.md](build-unix.md)
- Reproducible build and verification with GNU Guix: see [guix.md](guix.md)

Before running anything, **verify your binaries** against the published
`SHA256SUMS` file. GoldBrix builds are fully reproducible: anyone can rebuild
from the public source and obtain the exact same hashes. Matching hashes prove
the binary you downloaded corresponds bit-for-bit to the published source.

    sha256sum -c SHA256SUMS

## 2. Configure your node

Create a file named `goldbrix.conf` inside your data directory. A minimal
mining configuration looks like this:

    server=1
    daemon=1
    rpcuser=choose-a-username
    rpcpassword=choose-a-long-random-password

    # Connect to the network
    addnode=<seed-node-address>

`server=1` enables the RPC interface your miner will use. Choose a strong,
unique `rpcpassword`. The official seed node addresses are published at
https://goldbrix.app — add one or more with `addnode` so your node can find
peers and synchronize.

## 3. Start the node

    goldbrixd -daemon
    goldbrix-cli getblockchaininfo
    goldbrix-cli getnetworkinfo

Allow the node to synchronize: in `getblockchaininfo` the `blocks` value should
climb until it matches `headers`. You can confirm you are connected to the real
GoldBrix network by checking `getnetworkinfo` — it reports a `subversion` of
`/GoldBrix:.../`.

## 4. Create a payout address

Generate an address to receive your mining rewards, and keep it safe:

    goldbrix-cli getnewaddress

Every block you mine pays its reward to the address you configure in your
mining software.

## 5. Start mining

GoldBrix uses SHA-256d, so any standard SHA-256d miner can mine against your
node through the `getblocktemplate` workflow. Point your mining software at the
node's RPC interface, providing:

- the RPC host (usually `127.0.0.1` if the miner runs on the same machine),
- the RPC port (`8332`),
- the `rpcuser` and `rpcpassword` from your `goldbrix.conf`,
- your payout address.

For low-power CPU testing, a `getblocktemplate`-capable miner such as cpuminer
is enough to learn the workflow. For real hashrate, use ASIC mining software.

Monitor your mining and balance with:

    goldbrix-cli getmininginfo
    goldbrix-cli getbalance

The early reward window is deliberate: blocks before height 20,000 pay 50 GBX
each, rewarding the miners who bootstrap and secure the young network first.
After block 20,000 the reward drops to 0.25 GBX per block.

## Useful commands

    goldbrix-cli getblockcount          # current block height
    goldbrix-cli getdifficulty          # current network difficulty
    goldbrix-cli getconnectioncount     # number of connected peers
    goldbrix-cli getmininginfo          # mining and network hashrate info
    goldbrix-cli stop                   # cleanly shut the node down

## Why GoldBrix mining is fair

- **Open and permissionless** — anyone can mine. There is no allowlist, no
  gatekeeper, and no registration.
- **No premine, no presale** — every coin enters circulation only by being
  mined. Nothing was created or sold ahead of launch.
- **Fixed, transparent rules** — the supply cap, reward schedule, and
  difficulty algorithm are enforced by every node and are visible both in the
  consensus source code and on-chain.
- **Reproducible and verifiable** — the released binaries can be rebuilt from
  source and checked hash-for-hash (see [guix.md](guix.md)), so you never have
  to trust a binary blindly.

## License

GoldBrix Core is released under the MIT license. See [COPYING](../COPYING).
