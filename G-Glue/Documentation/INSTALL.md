# Installing G-Glue Bus Compressor

## Which file do I need?
| You use | Install | Built in this session? |
|---|---|---|
| MPC Desktop on **Windows** (incl. Controller Mode) | `G-Glue Bus Compressor.vst3` (Windows build) | No: built by the GitHub Actions workflow `gglue.yml` (`G-Glue-windows-x64` artifact) |
| MPC Desktop on **macOS** (incl. Controller Mode) | `G-Glue Bus Compressor.vst3` or `.component` (AU) (macOS build) | No: built by `gglue.yml` (`G-Glue-macos-universal` artifact) |
| A Linux DAW (Bitwig, Reaper, Ardour...) | Linux `G-Glue Bus Compressor.vst3` or `libG-Glue Bus Compressor.so` (VST2) | **Yes** |
| MPC Standalone (no computer) | nothing yet: see `MPC_STANDALONE_STATUS.md` | not possible without Akai's SDK |

MPC Desktop runs only on Windows and macOS, so **the Linux builds made here cannot be loaded by MPC Desktop**. Use the
Windows or macOS build from CI.

## Windows
1. Copy `G-Glue Bus Compressor.vst3` (the whole folder) to `C:\Program Files\Common Files\VST3\`.
2. Optional: copy the `FactoryPresets` folder anywhere. The factory presets are already built in; the files are for
   sharing and for importing elsewhere.

## macOS
1. VST3: copy `G-Glue Bus Compressor.vst3` to `/Library/Audio/Plug-Ins/VST3/` (or `~/Library/Audio/Plug-Ins/VST3/`).
2. AU: copy `G-Glue Bus Compressor.component` to `/Library/Audio/Plug-Ins/Components/`.
3. The CI build is ad-hoc signed, not notarised. If macOS blocks it, run
   `xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/G-Glue Bus Compressor.vst3"`, or sign and notarise it
   with your Developer ID before distributing it.

## Linux
- VST3: copy `G-Glue Bus Compressor.vst3` to `~/.vst3/`.
- VST2: copy `libG-Glue Bus Compressor.so` to `~/.vst/`.
- Standalone app: `G-Glue Bus Compressor` (runs with JACK or ALSA).
- Needs: libfreetype, libpng, libjpeg, libstdc++ (present on desktop distributions).

## User presets
User presets are saved as `.gglue` text files in `Documents/G-Glue/Presets` (on Linux `~/Documents/G-Glue/Presets`,
or `$GGLUE_PRESET_DIR` if that is set). Favourites are in `favorites.txt` in the same folder. Copy the folder to
move your presets to another computer.

---

# MPC Controller Mode setup (MPC Desktop + MPC hardware)

Not tested with MPC hardware in this session. The steps below follow MPC Desktop's documented plugin workflow;
menu names can differ between MPC 2.x and MPC 3.

1. Install the Windows or macOS VST3 as above.
2. In MPC Desktop: **Preferences → Plugins**. Make sure the VST3 folder is scanned (MPC 3 uses VST3; some MPC 2.x
   versions use VST2 / AU paths), then **Rescan**. "G-Glue Bus Compressor" by *G-Glue Audio* should appear under
   effects.
3. Connect the MPC by USB and switch it to **Controller Mode** (the MPC asks when MPC Desktop starts, or use the MPC
   menu → Controller Mode).
4. Insert G-Glue as an **insert effect** on a track, a program, a submix or the master (Channel Mixer → Inserts).
5. **Q-Links**: G-Glue's parameters are published in this fixed order, so hosts that map plugin parameters to Q-Links
   in order give:
   - Q-Link 1 = Threshold, 2 = Makeup, 3 = Attack, 4 = Release
   - then Ratio, SC Filter, Mix, Input, Output, Analog, Bypass

   Otherwise assign them by hand in the Q-Link edit page.
6. **Automation / MIDI Learn**: all 11 parameters are automatable, with smoothed, click-free changes. Use MPC's
   automation (Write / Latch) or MIDI Learn on any of them.
7. **Saving**: the full state (all settings, the A/B slots and the preset name) is saved in the MPC project and
   restored when it loads.
8. **Display**: G-Glue's own window opens on the computer screen. Whether MPC Desktop shows a third-party plugin's
   window on the MPC's touch screen depends on the MPC software version and was not verified. The parameter values
   are short texts ("-12.0 dB", "AUTO", "4:1", "90 Hz") so they read well on the MPC's own parameter pages.
