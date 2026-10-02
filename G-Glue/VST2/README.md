# VST2 target

Built from the same JUCE project as VST3 (`juce_add_plugin(... FORMATS VST ...)` in `../CMakeLists.txt`), only when
`-DGGLUE_VST2_SDK_DIR=<folder>` is given.

## Status
| Platform | Status |
|---|---|
| Linux x86_64 | **Built and tested in this session** against the FST header (`fst-dev` package, GPL-3+): `libG-Glue Bus Compressor.so`, 21 host checks pass |
| Windows / macOS | Not built. Needs a VST2 SDK. Steinberg stopped licensing VST2 to new developers in 2018, so use the Steinberg SDK only if you already hold a VST2 licence. An FST-based build is GPL-3+ |
| MPC Desktop | MPC 3 uses VST3. Use the VST3 build for MPC Desktop and Controller Mode. Older MPC 2.x versions also loaded VST2. |

## FST patch (`PatchFst.cmake`)
FST declares `VstSpeakerArrangement::speakers` as a flexible array (`speakers[]`), so `sizeof (VstSpeakerArrangement)`
is 8 bytes. JUCE allocates speaker arrangements as `sizeof (VstSpeakerArrangement) + (max (8, n) - 8) * 112`, which
assumes Steinberg's `speakers[8]`. With the unpatched header that is 8 bytes for stereo, and JUCE then writes two
112-byte speaker records into it: a heap overflow in the plugin whenever a host asks for its speaker arrangement.
This was found by this project's host test (glibc aborted with "buffer overflow detected").

`PatchFst.cmake` copies `fst.h` into the build folder with `speakers[8]` (the Steinberg ABI layout; each record is
112 bytes either way) and builds against that copy. Nothing is modified in the system or vendored into the
repository. With a Steinberg SDK folder the patch is skipped.
