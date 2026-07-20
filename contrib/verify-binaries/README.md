### Verify Binaries

GoldBrix releases are verified through two independent mechanisms - no trusted
signer is required:

1. **Checksum verification.** Every release publishes a SHA256SUMS file
   alongside its binaries on
   [GitHub Releases](https://github.com/GOLDBRIX-GBX/goldbrix-core/releases/latest),
   mirrored at [goldbrix.app/downloads](https://goldbrix.app/downloads/).
   Download the archive, then:

       sha256sum -c SHA256SUMS-v31-gbx-launchpad.txt

2. **Reproducible rebuild (the strong check).** The build is fully
   reproducible with GNU Guix (see [contrib/guix](../guix)). Rebuild the
   tagged source yourself and compare your output hashes to the published
   checksums - byte-for-byte equality proves the distributed binary is
   exactly this source, with no trust in any download server or key holder.

You can also compare the hash of the binary a public node is actually
running: every node health endpoint reports its binary_sha256 (see
[goldbrix.app/gbx-node-info](https://goldbrix.app/gbx-node-info)).
