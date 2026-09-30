# Da Lufs Plug: design notes

## Measurement (src/Loudness.h)
- **K-weighting** (BS.1770-4): a high shelf (+4 dB above ~1.7 kHz) followed by the RLB high-pass (~38 Hz).
  Coefficients are computed from the analog prototypes, so they are exact at any sample rate. Filters run in 64-bit.
- **Sub-blocks:** mean square of L and R (both weighted 1.0) every 100 ms.
- **Momentary** is 4 sub-blocks (400 ms) and **Short Term** is 30 (3 s). Both update every 100 ms, and their maxima
  are kept.
- **Integrated:** 400 ms gating blocks with 75% overlap, an absolute gate at -70 LUFS and a relative gate 10 LU below.
  It is kept in a 0.1 LU histogram of counts and energy sums, so memory stays fixed however long the song is.
- **Loudness Range** (EBU Tech 3342): short-term values every 100 ms, gated at -70 LUFS absolute and -20 LU relative.
  LRA = 95th minus 10th percentile, from a second histogram.
- **True Peak** (BS.1770-4 Annex 2):
  - oversampling reaches at least 176.4 kHz: 4x at 44.1/48 kHz, 2x at 88.2/96 kHz, none at 176.4/192 kHz
  - polyphase windowed sinc, 12 taps per phase, Blackman window, unity DC gain per phase
  - phase 0 is the original sample, and the maximum absolute value across phases and both channels is kept

Test results (test/host_test.cpp):
- A stereo 997 Hz sine at -23 or -33 dBFS reads -23.0 or -33.0 LUFS on M, S and I.
- The -36/-23/-36 dBFS gating case integrates to -23.0, and silence is gated out.
- 0 dBFS in one channel reads -3.01 LUFS.
- LRA reads 10 LU and 5 LU in the two level-step cases.
- An fs/4 sine at 45° phase shows its true peak, not its sample peak, at 44.1, 48 and 96 kHz.
- Integrated is correct at 48, 88.2 and 192 kHz.

## Plugin (src/dalufsplug.cpp)
- **Audio** is copied through untouched. Metering runs on the audio thread, and Pause stops feeding the meters.
- **Reset** is a request flag that the audio thread picks up at the next block.
- **Platforms** are the plugin's programs and its Platform parameter (index 0). Each sets a target and a true-peak
  ceiling; Custom keeps whatever you set. Because Platform is restored first, a saved custom target/ceiling survives
  when MPC restores the project.
- **Status lines** depend on the platform's policy: down only (YouTube, Amazon, Tidal, Deezer), up and down (Spotify,
  Apple Music, podcasts), reference only (TikTok range, broadcast, CD) or none (SoundCloud: plays as delivered).
  Spotify switches to a -2 dBTP ceiling for masters louder than its target. The text is
  double-buffered, and a changing parameter value tells MPC to fetch the new text.
- **Display:** readouts, the LED bar (momentary vs target, ±18 LU) and the 60 history columns (one per second, short
  term vs target, ±12 LU) are read-only parameters. They are sent to MPC only when they change, at 10 Hz at most.

## Skin
- **Layout:** the LED bar is 36 one-LU segments bound to the same parameter. Each segment's frames are a single solid
  colour, so the bar stays correct whatever offset MPC uses when slicing tall filmstrips (1.0.3 used 4 tall sections,
  and on an MPC X the top section showed its lit part at the wrong end).
- **Readouts:** big teal readouts are Titillium labels in bevelled wells. Platform, Target and Ceiling are value boxes
  with transparent drag strips.
- **Memory:** about 31 MB of images when decoded.
