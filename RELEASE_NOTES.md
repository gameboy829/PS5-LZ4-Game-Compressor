# LZ4 Game Compressor beta.2

Compress supported PS5 games directly on your console into AMPR LZ4 asset packs. Level 7 and content verification remain the defaults.

**Supported sources: this app can only convert mountable PS5 game images and extracted PS5 game folder dumps.** Images must be mountable by ShadowMountPlus. Folder dumps need the game's `eboot.bin`, `sce_sys/param.json`, and game files. Already converted AMPR folders are excluded from the source library.

## What's new and fixed

- **One ELF to load:** `LZ4-Game-Compressor-beta.2.elf` starts the compressor and installs or refreshes its Media shortcut and logo.
- **Folder dump support:** library entries show Image or Folder Dump. Dumps load automatically through a read-only view and display Folder Dump Loaded.
- **Better game discovery:** USB and M.2 game folders are discovered, including dumps containing a source `ampr_emu.index`. Converted AMPR output is kept out of the source list.
- **M.2 destinations:** use a PS5-recognized expansion SSD and choose custom destination folders, including `/mnt/ext1/homebrew`. Selecting a custom folder keeps the destination drive selection consistent.
- **Shared browser settings:** game selection, mount state, destination folders, compression level, verification, and original-game options synchronize when using the same PS5 address on multiple devices.
- **Complete remote progress:** opening the app during a conversion shows its current stage, game and file, speeds, elapsed time, estimated remaining time, and progress. Idle browsers continue checking for a conversion started elsewhere.
- **Clearer speeds and cancellation:** transfer rates use storage units per second, and partial-output cleanup reports whether deletion succeeded.

## Before converting

Keep both copies and leave verification enabled when trying a game for the first time. Folder dumps currently support Keep both; original-image move, delete, and swap options remain available for image sources. Verification checks content integrity and does not guarantee that a game will launch or play correctly. Deleting an original is permanent, and interrupted transfers may leave folders that need review.

Load the new ELF with Payload Manager after stopping any active conversion. The Media shortcut opens the interface; the payload must be running. If a browser still shows the old interface, reload it.

**This is a beta release.** If you encounter any bugs or unexpected behavior, please let me know so I can investigate and improve future releases.

Licensed dependencies and acknowledgments are included in the source archive, CREDITS.md, and THIRD_PARTY_NOTICES.md. No game content or proprietary Sony SDK is included.
