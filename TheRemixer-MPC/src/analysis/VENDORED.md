# Vendored analysis modules

Copied unchanged from the Velvet Interpolator project (`velvet-interpolator/src/`):
chordcore, keys, notes, poly, hpss and musicout (`.c` and `.h`).
They are plain C with no dependencies beyond libc and libm, and they already run on the MPC X in that plugin.
If one changes upstream, copy it again here and rerun `make test`.

Also copied unchanged: padsend (`.c` and `.h`), which turns a pad note schedule into VST MIDI events.

Local change in chordcore: `cc_segment_fit` was added (and the bin and template setup moved into helpers it shares
with `cc_analyze`). The output of `cc_analyze` is unchanged. The change is not in Velvet Interpolator.
