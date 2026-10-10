# Building

## PS5 payload

Use Linux/WSL, the open PS5 payload SDK, its supported LLVM toolchain, Make and Python 3.

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
bash tools/build-ps5.sh
```

Output: ampr-ps5/lz4-game-compressor.elf and ampr-ps5/lz4-launcher-installer.elf. The build embeds the UI and licensed AMPR runtime. Source mounting is delegated to ShadowMountPlus. The previous third-party application modules are not built.

LLVM 18 and the locally installed payload SDK compiled this version. SDK/compiler changes may need adjustments. No proprietary Sony SDK is included.

## Windows

```powershell
./ampr-native/build-portable.ps1 -Toolchain C:/path/to/llvm-mingw -GuiName ampr-gui-beta.exe
```

Outputs are under ampr-native/bin. Keep resources beside bin. User-supplied TOML profiles can be selected in the GUI; copied upstream wrapper profiles are not included.

## Local checks

The host test service is compiled with STUDIO_HOST_TEST, port 15911, a mock ShadowMount port 10102 and AMPR_STATE_ROOT set to a dedicated temporary directory. That build cannot check PS5 game-process APIs. See ampr-ps5/tests/test_hash.py, test_standalone.py and test_profile_roundtrip.py.
