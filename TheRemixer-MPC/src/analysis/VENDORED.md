# Vendored analysis modules

Copied unchanged from the Velvet Interpolator project (`velvet-interpolator/src/`):
chordcore, keys, notes, poly, hpss and musicout (`.c` and `.h`).
They are plain C with no dependencies beyond libc and libm, and they already run on the MPC X in that plugin.
If one changes upstream, copy it again here and rerun `make test`.
