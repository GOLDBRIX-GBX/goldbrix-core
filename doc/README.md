GoldBrix Core
=============

Setup
-----

GoldBrix Core is the reference node and wallet for the GoldBrix network. It
downloads and validates the entire block chain. Initial synchronization time
and disk usage depend on your hardware and connection.

Running
-------

GoldBrix Core ships the following executables:

- `goldbrixd`: headless daemon (node and wallet RPC).
- `goldbrix-qt`: optional graphical interface.
- `goldbrix-cli`: command-line RPC client.
- `goldbrix-tx`: transaction creation and parsing utility.
- `goldbrix-wallet`: offline wallet tool.
- `goldbrix-util`: stateless utility tool.

Building
--------

Build instructions live in this directory:

- Unix/Linux: [build-unix.md](build-unix.md)
- macOS: [build-osx.md](build-osx.md)
- BSD: [build-freebsd.md](build-freebsd.md), [build-openbsd.md](build-openbsd.md), [build-netbsd.md](build-netbsd.md)
- Windows: [build-windows.md](build-windows.md)
- Reproducible builds (GNU Guix): [guix.md](guix.md)

License
-------

GoldBrix Core is released under the terms of the MIT license. See
[COPYING](../COPYING) for details.
