# LZ4 Game Compressor beta.7

Private reports now include ShadowMount’s debug log when available, alongside the compressor diagnostics and app tab images. This helps investigate game-launch and mount failures.

The app reads /data/shadowmount/debug.log only; it does not enable logging or change ShadowMount settings. The report review includes its contents, availability, original size and whether it was truncated. Large logs include the last 1 MiB. Reports can include game names, paths and crash details and require your consent before sending privately.

Update through Updates → Check for updates → Install and restart. After reproducing a problem, use Report issue to submit a fresh report.

This beta converts only mountable PS5 game images and extracted folder dumps. Keep backups; verification does not guarantee launch compatibility.
