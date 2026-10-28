# Building KoniSynth

KoniSynth uses CMake. JUCE 9.0.4 and clap-juce-extensions are downloaded by CMake (FetchContent) on the first configure;
Munt mt32emu, Nuked-OPLL and the MTS-ESP client are in `Source/ThirdParty`.

Targets:

| Target | Output |
|---|---|
| `KoniSynth_VST3` | VST3 plugin |
| `KoniSynth_CLAP` | CLAP plugin (option `KONISYNTH_CLAP`, on by default) |
| `KoniSynth_Standalone` | standalone application |
| `KoniBench` | command-line renderer used by the tests (loads the VST3) |

Artefacts end up in `build/KoniSynth_artefacts/Release/<format>/`.

## Windows (x64), tested

Requirements: Visual Studio 2022 or newer (or the Build Tools) with the C++ workload (it includes CMake), Git.

```
cmake -S . -B build
cmake --build build --config Release --target KoniSynth_VST3 KoniSynth_CLAP KoniSynth_Standalone
```

The C++ runtime is linked statically (`/MT`): the binaries need no Visual C++ Redistributable.

**Faster rebuilds while developing (optional).** In Release, JUCE links with full link-time code generation, which redoes
the whole code generation for each format after any change. `-DKONISYNTH_FAST_LINK=ON` (off by default; MSVC only)
switches to incremental LTCG (`/LTCG:INCREMENTAL`), which regenerates only the functions that changed:

```
cmake -S . -B build -DKONISYNTH_FAST_LINK=ON
```

| Link mode | Rebuild after a one-file change (VST3 + Standalone, measured) | CPU in the heaviest KoniBench scenarios |
|---|---|---|
| Full LTCG (default) | 130 s | reference |
| Incremental (`KONISYNTH_FAST_LINK=ON`) | 34 s | +18 to +23 % |

The sound is the same (identical regression renders), but the binaries are slower: use it only between development
tests. Release builds and CPU measurements use the default full LTCG; after switching back to `OFF`, the first link
takes about 57 s per format.

## Linux (x86-64), tested

Built on Ubuntu 24.04 with GCC 13.3 (also inside WSL 2). The standalone application and the VST3 have been tested by
hand on Ubuntu. Packages:

```
sudo apt install build-essential cmake ninja-build pkg-config git \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev
```

WebKit and curl are not needed: the project builds with `JUCE_WEB_BROWSER=0` and `JUCE_USE_CURL=0`.

```
cmake -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --target KoniSynth_VST3 KoniSynth_CLAP KoniSynth_Standalone
```

Install: `KoniSynth.vst3` into `~/.vst3/`, `KoniSynth.clap` into `~/.clap/`. A binary built on Ubuntu 24.04 needs glibc 2.38 or
newer (measured with objdump); build on an older distribution (or in a container) to support older systems. MTS-ESP microtuning loads
`/usr/local/lib/libMTS.so` if it is installed.

## macOS, not built yet

There is no macOS build: it requires a Mac with Xcode (Apple's SDK may only be used on Apple hardware). What is ready:

- Apple Silicon: the AVX2 oscillator path is x86-only. `Source/SimdAvx2.cpp` compiles empty functions on ARM, and the plugin
  never calls them there, because it checks for AVX2 at run time. CMake passes `-mavx2` only for the x86_64 slice
  (`-Xarch_x86_64`).
- Munt, Nuked-OPLL and the MTS-ESP client have macOS code paths. The agent dialog and bridge use `~/Library` (JUCE's
  application data folder) and `.venv/bin/python`.

Untested steps for a Mac:

```
cmake -S . -B build-mac -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-mac --config Release --target KoniSynth_VST3 KoniSynth_CLAP KoniSynth_Standalone
```

Adding the Audio Unit format is one word in `CMakeLists.txt` (`FORMATS VST3 AU Standalone`). Without an Apple Developer ID
signature and notarization, macOS Gatekeeper blocks the plugin until its quarantine flag is removed
(`xattr -dr com.apple.quarantine KoniSynth.vst3`).

## Tests

```
python Tests/run_all.py                 # 10 suites (see README); needs KoniSynth_VST3 + KoniBench in Release and numpy
CLAP_VALIDATOR=<path> python Tests/run_all.py   # also runs clap-validator on KoniSynth_CLAP (0 failures with 0.4.1)
```

The test scripts run on Windows (they expect `build/.../KoniBench.exe`, see `Tests/common.py`); KoniBench has not been built on
Linux yet.

## Agent bridge (optional)

```
cd Agent/KoniSynth-mcp
python -m venv .venv
.venv/Scripts/python -m pip install -r requirements.txt     # Windows (Linux/macOS: .venv/bin/python)
```

Then register it in your MCP client; KoniSynth's *Agent (MCP)...* dialog copies the exact commands.
