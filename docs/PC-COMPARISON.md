# Compare a PC or PS5 conversion with Python

Run bin/ampr-gui.exe. Keep the resources folder beside bin.

1. Original game: select the original decoded game folder used by both conversions.
2. Native / PS5: select the completed native output, including a PS5 USB output.
3. Python reference: select the complete output from the pinned Python packer for the same original game.
4. Click Compare folders. This reads existing output; it does not repack it.

The GUI independently verifies both outputs against the original files, validates archive and decoded chunk CRCs, checks loose assets and the supported runtime replacement, then compares archive bytes. Assets are matched by path, so independent file-ID ordering and pack layout do not fail content comparison. Corrupted or missing assets fail.

Enable Require exact archive bytes when identical files and metadata are required. Use identical TOML settings, source index/order and compression implementation for byte equality. Generated audit profile names/text are reported separately and are not included in archive equality. Game launch compatibility is not established by this check.

The Original game field is required for independent content verification. This GUI compares supported AMPRPAK4 outputs from this project and the pinned Python packer; it is not a comparator for arbitrary archive formats. No title-specific rules are inferred from a game.

Compare after packing and Pack and compare remain available. Blank TOML uses Python-derived universal rules; selected TOML retains its own compression settings. Detailed diagnostic logs are saved to logs next to resources and bin.



Full original-folder coverage: before decoding, the comparison scans every original file and requires it to be indexed or copied loose in each output. Unindexed loose files are checked by size and SHA-256. Files missing from both manifests fail with their paths. Symbolic links are rejected. The source ampr_emu.index is treated explicitly as regenerated metadata and independently validated by the existing index/manifest checks; byte identity is still covered by Require exact archive bytes. This checks filesystem files, not PS5 launch compatibility or OS permissions.


