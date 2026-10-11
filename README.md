# LZ4 Game Compressor

**Beta 7 — On-console PS5 game compression**

<img src="logo.png" width="128" alt="LZ4 Game Compressor logo">

LZ4 Game Compressor lets you compress your PS5 games directly on your console, helping reduce the space they occupy without having to transfer them to a PC or create a separate, fully extracted copy.

It converts supported PS5 game sources into AMPR LZ4 asset packs, using ShadowMountPlus to mount game images when needed. Everything is managed through a browser-based interface that you can open on your PS5 or another device connected to the same local network.

## Supported sources

**This app can only convert mountable PS5 game images and extracted PS5 game folder dumps.** Images must be mountable by ShadowMountPlus. A folder dump must contain the game's `eboot.bin`, `sce_sys/param.json`, and game files.

Images are read through their mounted files; the compressor does not extract the image format itself. Folder dumps are loaded through a read-only view. Already converted AMPR folders are excluded from the source library.

## Getting started

### What you'll need

Before starting, make sure you have:

- A homebrew-enabled PS5
- Payload Manager
- ShadowMountPlus
- A mountable game image supported by ShadowMountPlus, or an extracted PS5 game folder dump
- Enough available storage for the converted game

Keep ShadowMountPlus installed for image mounting and library refresh. Folder dumps do not need an image mount.

### Launching the compressor

1. Load `LZ4-Game-Compressor.elf` using Payload Manager.
2. Open the compressor interface in your browser at:

   `http://<PS5_IP>:5911/`

   Replace `<PS5_IP>` with your console's local IP address.

3. Your game library should appear in the interface, ready for you to select a game.

The same ELF installs or refreshes the app icon and **Media shortcut** on your PS5, so no separate shortcut installer is needed. You can also access the interface using that shortcut. Keep in mind that the shortcut only opens the interface; the compressor payload must already be running.

## Compressing a game

The conversion process is designed to be straightforward, with most settings handled automatically.

### 1. Select your game

Find the game you want to compress in your library. Each entry is labeled **Image** or **Folder Dump** and shows its drive and source path.

Make sure no game is currently running. For an image, select **Mount game**. A folder dump loads automatically; the disabled button then reads **Folder Dump Loaded**.

### 2. Choose where to save it

Select the internal SSD, an available M.2 expansion SSD, or a connected USB storage device as your conversion destination. An M.2 drive must already be recognized and mounted by the PS5. Custom folders such as `/mnt/ext1/homebrew` can be selected under **Destination folders**.

You can customize the exact folder locations by expanding **Destination folders**. The compressor automatically checks whether your selected drives have enough free space before allowing the conversion to start.

### 3. Decide what happens to the original

You can keep the original game, delete it after conversion, move it somewhere else, or exchange its location with the compressed version.

If you're trying the compressor for the first time, **Keep both** is the safest choice. This lets you test the compressed game before deciding whether to remove the original.

See [Managing your original games](#managing-your-original-games) for more information about each option.

### 4. Start the conversion

Once everything is configured, select **Start conversion**.

The compressor will take care of the required file copying, LZ4 compression, AMPR runtime installation, and game-readable permissions.

You can follow the progress in real time, including conversion speed and estimated remaining time. These estimates reflect the steps selected for your particular conversion.

If something needs attention before starting, the interface will explain what's missing beneath the Start conversion button.

### 5. Verify and finish

**Content verification is enabled by default**, and it's recommended to leave it on.

Verification checks the converted content for data integrity before the compressor performs any requested operations on the original game.

You can skip verification, but the resulting game will be marked **Unverified**. A successful verification doesn't necessarily mean the game will launch or function correctly, so it's worth testing the converted game yourself.

Once conversion finishes, your compressed game will be saved to the selected location.

If you enabled **Restart ShadowMount after conversion**, the compressor will also restart ShadowMount to refresh its game library.

## Using the app from more than one device

Open the same PS5 IP address and port on each device. Game selection, destination folders, compression level, verification choices, original-game options, and mount state stay synchronized between browsers.

A conversion keeps running on the console when you close a browser. Opening the interface on another device shows the current stage, game, file, transfer speeds, elapsed time, estimated remaining time, and progress. Setup and Progress tab selection stays local to each browser.

## Managing your original games

You don't necessarily have to keep two copies of every game. The compressor provides several options for organizing your storage after a successful conversion.

| Option | What happens |
|---|---|
| **Keep both** | Your original game stays untouched, and the compressed game is saved to your chosen destination. |
| **Delete original** | The original is permanently removed once the selected conversion checks have completed successfully. You'll be asked to confirm this choice. |
| **Move original** | The original game is moved to another folder or drive after conversion. |
| **Swap original and converted** | The compressed game takes the original game's place, while the original is relocated to the destination drive. |

Folder dumps currently use **Keep both**. Moving, deleting, and swapping the original are available for image sources.

### Moving games between drives

When moving an original game to another drive, the compressor copies and verifies it before removing the old file. This helps protect against data loss during transfers.

The original game is unmounted before any operation that requires moving or deleting its image.

### Swapping game locations

The **Swap original and converted** option is useful when you want the compressed version to remain on your main game drive while keeping the original on another drive.

For example, you could compress a game from your internal SSD to a USB drive, then swap their locations so the compressed game ends up on the internal SSD and the original moves to USB.

Swapping is only available when the original and conversion destination are on different drives.

Because the process temporarily requires additional copies, both drives must have sufficient free space. Full conversion and returned-copy verification are required.

**Important:** Swapping is not an atomic operation. If it's interrupted, successfully copied files are preserved rather than automatically removed. You may need to review the resulting folders manually.

### Customizing folders

All folder settings are grouped under **Destination folders**:

- **Converted Game Folder:** Where the new compressed game is initially saved.
- **Original Game Folder:** Where the original is moved when using Move or Swap.
- **Final Converted Game Folder:** Where the converted game is placed on the original drive when swapping.

Only the folder options relevant to your selected operation are enabled.

## Compression settings

The compressor uses **LZ4 HC** compression, with level **7** selected by default.

LZ4 is designed for fast decompression, making it suitable for game assets that need to be accessed while playing. Actual storage savings depend on the game and its contents.

Conversions use a multithreaded pipeline with a maximum of eight threads, including the coordinator.

Most users can leave the default compression and verification settings unchanged.

## Canceling a conversion

You can cancel a conversion while it's running.

When canceling, the interface lets you choose whether to keep or delete the partially generated output. Keeping it may be useful for inspection, but an incomplete conversion should not be treated as a playable game.

If a conversion fails, the compressor won't proceed with the selected move, delete, or swap operation on the original game.

Failed or canceled jobs may still leave partial or completed folders behind. Check the locations reported by the interface before attempting another conversion.

## Things to know

LZ4 Game Compressor is currently **beta software**. Although it includes storage checks and content verification, there are a few things worth keeping in mind:

- **Compression results vary.** Games containing already-compressed assets may see smaller reductions in size.
- **Verification isn't a compatibility test.** A verified conversion can still encounter issues when launching or playing.
- **Keep a backup when possible.** Especially when trying a game for the first time.
- **Deleting is permanent.** If you choose to remove your original, make sure you're comfortable relying on the converted copy.
- **Interrupted operations may need cleanup.** In particular, moving or swapping games between drives can leave extra copies if the process doesn't finish.

For the safest experience, start with **Keep both**, leave verification enabled, and test your compressed game before deleting the original.

## License and acknowledgments

LZ4 Game Compressor is distributed under the **GPL-3.0-or-later** license.

It incorporates licensed AMPR format, runtime, and packing code from [ampr_emu](https://github.com/drakmor/ampr_emu), along with third-party components including LZ4, PicoSHA2, nlohmann/json, and toml++.

Additional information is available in:

- [Credits and third-party acknowledgments](CREDITS.md)
- [License](LICENSE)
- [Source provenance](docs/PROVENANCE.md)
- [Build instructions](docs/BUILD.md)

When redistributing the application, preserve the applicable license notices and provide corresponding source code as required by the respective licenses.

No game content or proprietary Sony SDK is included.

## Reporting a problem

Choose **Report issue** in the app, enter a title and describe what happened. The report includes images of Setup and Progress, detected games and storage, dropdown choices, settings, compressor logs and ShadowMount’s debug log. Only the app interface is captured. Game names and file paths may appear in the report; game contents are not uploaded. Large logs include the last 1 MiB and are marked when truncated.

Review the image previews or choose **Review report** to inspect the complete report. Check the consent box and choose **Send private report**. Reports go through the project’s Supabase service to a private GitHub repository accessible to its owner and any collaborators they authorize. No GitHub login is needed. If submission fails, save the report and retry later.
