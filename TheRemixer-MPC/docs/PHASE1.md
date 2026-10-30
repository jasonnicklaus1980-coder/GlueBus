# The Remixer - Phase 1 status: MPC audio recording foundation

## Built and checked here (host tests, no MPC)
- VST2 effect, 2 in / 2 out, `uniqueID` 'TRMX'. Plugin entry in `mpc/plugin.xml`.
- Audio passes through bit-exact (checked sample by sample in `test/host_test.cpp`).
- Recording: the audio thread copies the stereo input into a lock-free ring buffer; a writer thread writes 24-bit
  PCM WAV. The audio thread never touches the disk, never allocates and never locks.
- Written WAV is checked: RIFF/WAVE header, PCM, stereo, 24-bit, sample rate, data length, and the recorded samples
  match the input to 24-bit precision (max error 6e-8 on a 1.4 s test).
- Read-only meters: Peak L/R (dBFS), clip count, recording time, dropout count, status.
- Installer, uninstaller and package script in the same layout as the other plugins in this repo.

## Not verified yet (needs the MPC X)
1. **Hardware input reaching the plugin.** The plugin declares a stereo input. Whether the MPC feeds the physical
   input (mic, keys, guitar) to it through the track input routing, and at what level, is unknown until it runs on
   the device.
2. **Sample rate.** The code uses the rate the host gives in `effSetSampleRate` and writes it into the WAV header.
   The rate the MPC actually runs at is unknown.
3. **Long recordings.** Dropouts are counted when the disk falls behind the audio. The test runs only about 1.4 s.
   A multi-minute test is needed on the device's storage.
4. **Stop latency.** Stopping waits 50 ms so audio already in flight is written before the file closes. Check that the
   last sounds aren't cut off.
5. **Threading.** `setParameter` runs on the host's control thread and starts or stops the writer thread there. Confirm
   the MPC calls `setParameter` from a thread that is allowed to block for the 50 ms stop.
6. **WAV compatibility.** The MPC must load and play the 24-bit WAV. Check that it appears in the sample browser.
7. **Plugin loading on the device.** The ARM build is verified (see below), but it has not been loaded by the MPC itself.

## ARM verification (cross-built and run under QEMU, not on the device)
- `make arm`: ELF32, ARM EABI5, ARMv7, VFP hard-float; needs only libc and libm; highest glibc symbol GLIBC_2.34.
- The plugin host test (`test/host_test.cpp`), built for ARM and run under `qemu-arm`, passes: bit-exact passthrough,
  valid 24-bit WAV, samples match the input to 24-bit precision.
- The analysis tests, built for ARM and run under `qemu-arm`, pass with the same results as on the host.
- `scripts/package.sh` produces `TheRemixer-0.1.0-mpc-armv7.zip`; all six files match `SHA256SUMS`.
- Emulation checks the code's behaviour, not its speed. Timing on the MPC X is still unmeasured.

## Design decisions for later phases
- 24-bit PCM, not 32-bit float: the float WAV format is less certain on the MPC.
- No skin yet. The MPC shows the generic view. The skin is a later phase, and it must be verified on the device first.
- The recording path is in the plugin, not in a separate app, so recording works from any track the plugin sits on.

## Next steps
1. Confirm the 7 items above on the MPC X.
2. Phase 2 (analysis): key, BPM and chord recognition on the recording, starting from the existing `velvet-interpolator`
   analysis code.
