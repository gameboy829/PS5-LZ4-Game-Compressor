# Source provenance

The previous development payload linked PS5-Game-Compressor modules. That payload remains outside this release tree and is not the artifact built here.

The new Makefile links only standalone/main.cpp, src/ampr_backend.cpp, licensed LZ4 source and the PS5 SDK libraries. Backend .inc files and native headers are included by the backend. The separate shortcut installer builds only standalone/launcher.cpp against the SDK. Neither target builds gc_*, pfs_*, asset.c, the old installer, or the old SHA-256 implementation.

The application service and asset generator were implemented here. Profile construction now creates TOML from structured settings; extracted Lazy_AMPR wrapper templates have been removed. Functional AMPR configuration values, format behavior and interoperability requirements remain compatible targets, not a claim of archive-byte identity.

PicoSHA2 is pinned at 161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29 with its MIT license. JSON and TOML headers retain embedded MIT notices. LZ4 source retains its BSD license notices. ampr_emu source and license are bundled alongside its runtime. Its normal runtime SHA-256 is 69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a.

The source replacement was informed by previous project work and functional behavior; it is not represented as a formal clean-room legal certification. Console validation of the replacement application is pending. Earlier game launch results belong to the previous application and must not be presented as validation of this replacement ELF.
