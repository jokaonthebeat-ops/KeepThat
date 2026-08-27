# Building KEEP THAT! for Windows

The Mac this project is developed on cannot produce Windows binaries — there
is no MinGW, no clang-cl and no Wine on it. Windows builds come from either
GitHub Actions or a Windows machine. This is the Windows machine route.

## What you need

- **Visual Studio 2022** with the "Desktop development with C++" workload
  (the free Community edition is fine)
- **CMake 3.22+** — the VS installer can add it, or get it from cmake.org
- **Git**

## Build

```bat
git clone git@github.com:jokaonthebeat-ops/KeepThat.git
cd KeepThat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
```

CMake fetches JUCE 9.0.0 itself, so there is nothing else to install. The
first configure takes a few minutes while it clones.

Results land in:

```
build\KeepThat_artefacts\Release\VST3\KEEP THAT!.vst3
build\KeepThat_artefacts\Release\Standalone\KEEP THAT!.exe
```

The artwork is copied into both by a post-build step.

## Package for release

```powershell
$stage = "KEEP THAT!"
New-Item -ItemType Directory -Force -Path "$stage/VST3","$stage/Standalone" | Out-Null
Copy-Item -Recurse -Force "build/KeepThat_artefacts/Release/VST3/KEEP THAT!.vst3" "$stage/VST3/"
Copy-Item -Recurse -Force "build/KeepThat_artefacts/Release/Standalone/*"        "$stage/Standalone/"
Compress-Archive -Path "$stage/*" -DestinationPath "KeepThat-Windows.zip" -Force
```

Then attach `KeepThat-Windows.zip` to the GitHub release.

## Installing what you built

- **VST3** — copy `KEEP THAT!.vst3` into `C:\Program Files\Common Files\VST3\`
- **Standalone** — keep the `Resources` folder **next to** the `.exe`. That is
  where the artwork lives; `Assets.cpp` walks up from the running binary
  looking for `Resources/Assets`, which is the same rule the Mac bundles use.

## If it does not compile

This CMake build has never been run — there is no cmake on the development
machine to even syntax-check it. The likely first failures:

- **`JuceHeader.h` not found** — `juce_generate_juce_header(KeepThat)` must
  have run; check the target name matches.
- **Duplicate `JucePluginDefines.h`** — there is a hand-written one at the repo
  root belonging to the Makefile build. It must NOT be on the include path
  here; CMake generates its own. If it leaks in, move it under `packaging/`
  and point the Makefile at the new location.
- **Missing `juce_dsp`** — it is linked explicitly; the key detector and tempo
  detector both need it.

## Code signing

Unsigned Windows builds run, but SmartScreen warns on first launch. An EV or
standard code-signing certificate removes that. Nothing in this repository
signs the Windows binaries.
