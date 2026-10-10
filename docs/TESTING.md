# Local validation

The replacement compressor and independent shortcut installer compile using the open PS5 payload SDK and LLVM 18. They have not been installed on the console; the user has a conversion running.

Linux/WSL: run bash tools/build-host.sh. This compiles the host service and packing harness and runs 78 SHA-256 reference checks, HTTP/API integration checks and a synthetic native/Python AMPR interoperability test. The tests cover read-only rejection, mount request ordering, worker caps, malformed requests, data hashes, archive and executable permissions, overwrite rejection and damaged output detection.

Windows: after building, run python ampr-ps5/tests/test_windows_gui.py. It packs a synthetic fixture through the actual hidden GUI, checks exact comparison success, and rejects changed, missing and extra files.

Console validation still needs discovery, read-only mounting, synthetic conversion, final verification, cancellation cleanup, benchmark recommendations and Media shortcut registration. A full real-game launch test is also needed. Host checks do not verify firmware-specific API behavior or performance.

Beta.2 adds explicit verification-skip checks, Windows unverified-output comparison, and original-file fixtures covering deletion consent, extra unverified consent, cancellation, changed originals, existing backup refusal, symlink rejection, same-drive moves and verified cross-drive backups. All destructive tests use temporary synthetic files. Console original handling remains untested.
