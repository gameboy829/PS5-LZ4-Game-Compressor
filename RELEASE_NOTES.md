# LZ4 Game Compressor — beta.1

Experimental PS5 application for converting read-only mounted games into AMPR LZ4 asset packs without first creating a full extracted game copy.

## Included compression features

- LZ4 HC compression with level 7 as the default.
- Internal SSD and USB destinations with automatic free-space checks.
- Source-game drive and path shown in the game library.
- Controller-friendly browser interface and optional Media shortcut.
- Progress and time estimates for only the selected conversion steps.
- Multithreaded packing and required loose-file copying.
- Optional content verification, enabled by default; skipped checks mark output Unverified.
- Keep, move, delete, or swap the original and converted games across drives.
- All relevant locations in Destination folders.
- Verified cross-drive original moves and mandatory verification for swaps.
- Optional ShadowMount restart and library scan after successful completion.
- Cancellation with a choice to clean up partial output.
- AMPR runtime installation and required file permissions.

## Requirements and use

Requires a homebrew-enabled PS5, Payload Manager, and ShadowMountPlus. Load `LZ4-Game-Compressor-beta.1.elf`, open `http://<PS5_IP>:5911/`, select and mount a game read-only, choose destinations and original-file handling, then start conversion. The shortcut requires the payload to be running.

## Beta notice

Compatibility, compression ratio, speed, and recovery are not guaranteed. Keep independent backups until launch and gameplay are confirmed. Verification checks data integrity; it does not prove a game will boot. Deleting the original is permanent, particularly risky when verification is skipped.

Swaps require free space for temporary copies and full verification. File moves and swaps are not atomic. On failure or cancellation, review retained folders and reported locations before retrying. Existing destination folders are never overwritten.

## Release files

- `LZ4-Game-Compressor-beta.1.elf` — PS5 compressor payload.
- `LZ4-Game-Compressor-Shortcut-beta.1.elf` — Media shortcut installer.
- `LZ4-Game-Compressor-source.zip` — source, build scripts, dependency notices, and AMPR runtime source.
- `LICENSE` and `CREDITS.md` — license terms and attribution.
- `SHA256SUMS.txt` — release file checksums.

## License and credits

GPL-3.0-or-later application and AMPR-derived engine, with licensed ampr_emu, LZ4, PicoSHA2, nlohmann/json, and toml++ dependencies. ShadowMountPlus and Payload Manager are external services. PS5-Game-Compressor and Lazy_AMPR are workflow inspirations; their unlicensed application sources are not included. See CREDITS.md and the included source licenses.
