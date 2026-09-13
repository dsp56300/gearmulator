# openSUSE Open Build Service packaging

These files are the OBS package `home:theusualsuspects/TheUsualSuspects`:
`_service`, `_constraints`, `TheUsualSuspects.spec` and `.changes` for the RPM
distributions, `TheUsualSuspects.dsc` plus `debian.*` for Debian and Ubuntu, and `PKGBUILD` for
Arch. OBS picks the recipe per target from the same package.

One build per target produces one package per synth, each holding that synth's
VST2, VST3, CLAP and LV2 plugins: `theusualsuspects-osirus`, `-ostirus`,
`-vavra`, `-xenia`, `-nodalred2x` and `-je8086`. Adding a synth means a
subpackage in the spec, a `Package:` stanza in `debian.control`, a
`debian.<package>.install` file, an entry in the dsc's `Binary:` list and a
package function in the `PKGBUILD`.

## Targets

| Distribution | x86_64 | aarch64 |
|---|---|---|
| openSUSE Tumbleweed | `openSUSE_Tumbleweed` | `openSUSE_Factory_ARM` |
| openSUSE Leap 16.0 | `16.0` | `16.0` |
| Fedora 43, 44 | `Fedora_43`, `Fedora_44` | same repositories |
| Debian 12, 13 | `Debian_12`, `Debian_13` | not offered by OBS |
| Ubuntu 22.04, 24.04, 26.04 | `xUbuntu_22.04`, `xUbuntu_24.04`, `xUbuntu_26.04` | not offered by OBS |
| Arch Linux | `Arch` | not offered by OBS |

The names are the repository directories under
https://download.opensuse.org/repositories/home:/theusualsuspects/. OBS builds
Debian and Ubuntu for x86_64 only, and Tumbleweed's aarch64 port lives in a
separate project, hence its own repository.

## Which repository for which distribution

Most well-known distributions are rebuilds of one of the targets above and install our
packages from that base's repository. Match the base release exactly: from Ubuntu 24.04 and
Debian 13 on, the packages depend on `libasound2t64`, which older bases do not have.

| Distribution | Repository |
|---|---|
| Kubuntu, Xubuntu, Lubuntu, Ubuntu Studio | the `xUbuntu_` repository with the same release number |
| Linux Mint | the Ubuntu LTS it is built on: Mint 21 → `xUbuntu_22.04`, Mint 22 → `xUbuntu_24.04` |
| Pop!_OS, elementary OS, Zorin OS, KDE neon | the Ubuntu LTS it is built on, e.g. Pop!_OS 22.04 → `xUbuntu_22.04` |
| LMDE, MX Linux, AV Linux | the Debian release it is built on, e.g. LMDE 6 → `Debian_12` |
| Nobara, Ultramarine | the Fedora release with the same number |
| EndeavourOS, CachyOS, Garuda, Manjaro | `Arch` (Manjaro holds updates back for weeks, so a fresh build can occasionally need a newer library than it has) |
| openSUSE Slowroll | not built; the Tumbleweed repository usually installs |

Not covered:

- **Raspberry Pi OS, Zynthian and any other arm64 Debian or Ubuntu**: OBS builds neither for
  aarch64; use the portable Linux aarch64 builds.
- **Immutable distributions and Flatpak DAWs** (Fedora Silverblue, Bazzite, SteamOS, openSUSE
  Aeon): a sandboxed DAW only loads plugins shipped as Flatpak extensions.

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

- `_constraints` asks for workers with 30 GB of disk and 8 GB of RAM; a full
  x86_64 build used 20 to 24 GB and 4 to 5 GB.
- Debian's `dh` configures with `CMAKE_BUILD_TYPE=None`, which is why
  `base.cmake` defines NDEBUG for every non-Debug build.
- VST2 builds against the bundled GPL-3.0 FST headers.
- Automatic dbgsym packages are off in `debian.rules`; one synth alone produced
  378 MB of them. OBS disables debuginfo on the RPM side as well.
- Build times depend on the worker: 25 to 95 minutes per target on x86_64.
- Vendored libraries are statically linked. That is fine for `home:` projects and
  `multimedia:proaudio`, and only a hurdle for openSUSE:Factory.
- No standalone builds yet: they would need `.desktop` files, icons and AppStream
  metadata.
- No firmware is included.
