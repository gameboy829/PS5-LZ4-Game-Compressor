# LZ4 Game Compressor — beta.1

<img src="logo.png" width="128" alt="LZ4 Game Compressor logo">

Convert mounted PS5 games into AMPR LZ4 asset packs directly on the console, without creating a full extracted copy first. Use the browser interface with a controller or another device on your local network.

## Compression features

- LZ4 HC compression, with level **7** as the default.
- Internal SSD or USB destinations, with automatic space checks.
- Game library entries show the original image's drive and source path.
- Progress, speed, and time estimates for only the selected conversion steps.
- Multithreaded conversion, limited to eight pipeline threads including the coordinator.
- Required loose-file copying, AMPR runtime installation, and game-readable permissions.
- Content verification enabled by default; skipped verification marks output **Unverified**.
- Keep, move, delete, or swap the original and converted games.
- All folder choices under **Destination folders**.
- Optional **Restart ShadowMount after conversion** to refresh its game library.
- Cancel a conversion and choose whether to delete its partial output.

## Requirements and use

Requires a homebrew-enabled PS5, Payload Manager, and ShadowMountPlus. The source image must be supported by ShadowMountPlus and mounted read-only. The compressor reads its mounted filesystem; it does not unpack image formats itself.

1. Load `LZ4-Game-Compressor-beta.1.elf` through Payload Manager.
2. Open `http://<PS5_IP>:5911/` in a browser. The Media shortcut opens this interface and requires the payload to be running.
3. Close any running game, select the original game, and press **Mount game**.
4. Choose destination storage and what to do with the original.
5. Review locations under **Destination folders**. Space checks run automatically.
6. Leave verification enabled and press **Start conversion**. Missing requirements appear below the button.

## Original game choices

| Choice | After successful conversion |
| --- | --- |
| **Keep both** | Leave the original where it is. |
| **Delete original** | Delete the original after selected checks finish. Confirmation is required. |
| **Move original** | Move it to the selected **Original Game Folder**. Cross-drive moves copy and verify it before removing the old file. |
| **Swap original and converted** | Return the verified converted game to the original drive, move and verify the original on the destination drive, then remove the temporary converted copy. |

Swap is available only when the original and conversion destination are on different drives. Full conversion and returned-copy verification are required. Both drives need space for temporary copies. Swapping is not atomic; completed copies are retained if the operation fails or is canceled.

**Converted Game Folder** is always available. **Original Game Folder** is enabled for moving or swapping. Swap also enables a final converted-game folder on the original drive. The original is unmounted before moving or deleting it. Failed conversion does not trigger original-file actions.

## Beta notice

Experimental software: game compatibility, compression ratio, speed, and recovery are not guaranteed. Keep independent backups until the converted game launches and plays correctly. Verification establishes data integrity, not game compatibility.

Deleting the original is permanent. Skipping verification and deleting it can leave you without a usable copy. Failed or canceled jobs can leave partial or completed folders; review reported locations before retrying. The Media shortcut does not start the payload itself.

## License and credits

Distributed under **GPL-3.0-or-later**, with licensed AMPR format/runtime and packing code from [ampr_emu](https://github.com/drakmor/ampr_emu). LZ4, PicoSHA2, nlohmann/json, and toml++ retain their own licenses and notices.

See [CREDITS.md](CREDITS.md), [LICENSE](LICENSE), [source provenance](docs/PROVENANCE.md), and [build instructions](docs/BUILD.md). Preserve license notices and provide corresponding source with binary distribution as required by the licenses. No game content or proprietary Sony SDK is supplied.
