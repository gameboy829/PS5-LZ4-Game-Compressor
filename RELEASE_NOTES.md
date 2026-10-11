# LZ4 Game Compressor beta.3

This release fixes the missing Media shortcut by using the newer PS5 app registration method. Registration was verified on firmware 12.20, and a user confirmed that the shortcut works after a clean installation. Other firmware versions still need confirmation.

Download **LZ4-Game-Compressor.elf** and load it with Payload Manager after stopping any active conversion. This single payload starts the compressor and installs or refreshes its Media shortcut. Reload an open browser to see the updated interface. The shortcut opens the local interface; the payload must be running.

Also included:

- **Updates** in the app: check GitHub releases and download, install, and restart a newer version. Downloads use HTTPS and SHA-256 verification. Release checking was tested on the PS5; a complete self-update between published versions has not yet been tested on-console.
- Completed conversions stop the elapsed timer and show a completion message with the total duration.
- The ELF filename stays the same across releases.

**Supported sources are mountable PS5 game images and extracted game folder dumps.** Images must be mountable by ShadowMountPlus; dumps need `eboot.bin`, `sce_sys/param.json`, and the game files. Level 7 and verification remain the defaults.

This fix addresses shortcut registration. It does not claim to fix the separate folder-loading or connection-loss reports. This is beta software: keep your original game when trying a conversion for the first time. Content verification does not guarantee that every game will launch correctly.

Dependencies and credits are documented in the repository's CREDITS.md and THIRD_PARTY_NOTICES.md. No game content or proprietary Sony SDK is included.
