# openSUSE Open Build Service packaging

These files are the OBS package `home:theusualsuspects/TheUsualSuspects`:
`_service`, `TheUsualSuspects.spec` and `.changes` for openSUSE, and
`TheUsualSuspects.dsc` plus `debian.*` for Debian and Ubuntu. OBS picks the spec
or the dsc per target from the same package.

## Source

`_service` runs `tar_scm` on `main` with submodules, then `recompress` to xz,
both server-side, so every service run builds the current `main`. `obs_scm` with
`tar` does not work here: `tar` exists neither on the OBS source host nor in the
Debian/Ubuntu build chroots, and `dpkg-source` rejects an uncompressed orig
tarball. The build itself is offline and fetches nothing.

## Working with it

`osc` does not run on Windows (it imports `fcntl`); use it from WSL:

    osc checkout home:theusualsuspects TheUsualSuspects
    osc service remoterun home:theusualsuspects TheUsualSuspects   # rebuild from current main
    osc results home:theusualsuspects TheUsualSuspects

## Bumping the version

`project(gearmulator VERSION x.y.z)` in the top-level `CMakeLists.txt` is the
source of truth. Mirror it in `_service` (`version`), the spec (`Version:`), the
dsc (`Version:` and `DEBTRANSFORM-TAR:`) and a new `debian.changelog` entry.

## Notes

- Debian's `dh` configures with `CMAKE_BUILD_TYPE=None`, which is why
  `base.cmake` defines NDEBUG for every non-Debug build.
- VST2 builds against the bundled GPL-3.0 FST headers.
- Automatic dbgsym packages are off in `debian.rules`; one synth alone produced
  378 MB of them. OBS disables debuginfo on the RPM side as well.
- Build times depend on the worker: 25 to 90 minutes per target.
- Everything ships as one package; the Debian one is about 420 MB. Per-synth
  subpackages would let users install a single emulator.
- Vendored libraries are statically linked. That is fine for `home:` projects and
  `multimedia:proaudio`, and only a hurdle for openSUSE:Factory.
- No standalone builds yet: they would need `.desktop` files, icons and AppStream
  metadata.
- No firmware is included.
