# Repository restructure plan

Status: **proposal, nothing moved yet**

Decided: codenames per §5 (`axel` / `waldi` / `claudia` / `enzo` / `novak`), and
`framework/{core-level, juce/, tools/}` per §3. Everything else still open.

## 1. What is there today

`source/` has **60 top-level entries** — 47 directories and 13 loose build files — all
siblings. Nothing in the layout says which of them is ours, which is borrowed, which
belongs to which synth. Five things are already grouped (`3rdparty/`, `nord/`,
`ronaldo/`, and the nested `n2x/`, `je8086/`); the other 42 are flat.

Sorted by what they actually are:

| Kind | Entries |
|---|---|
| **3rd party, submodule, pristine upstream** | `3rdparty/freetype`, `3rdparty/lunasvg` |
| **3rd party, submodule, our fork** (`github.com/dsp56300/*`) | `JUCE`, `cpp-terminal`, `clap-juce-extensions`, `3rdparty/RmlUi` |
| **3rd party, vendored in tree** (no upstream link) | `fst`, `libresample`, `portaudio`, `portmidi`, `ptypes`, `3rdparty/lua`, `vstsdk2.4.2` (fetch stub) |
| **Ours — processor emulators** (submodules we own) | `dsp56300`, `mc68k`, `ronaldo/h8s`, (`h8sEmu` on `device/nova`) |
| **Ours — framework** | `baseLib`, `synthLib`, `hardwareLib`, `networkLib`, `mcpServerLib`, `jucePluginLib`, `jucePluginEditorLib`, `juceRmlUi`, `juceRmlPlugin`, `juceUiLib`, `jucePluginData`, `bridge`, `pluginTester`, `midiLearnTest`, `changelogGenerator` |
| **Access** | `virusLib`, `virusJucePlugin`, `virusConsoleLib`, `virusTestConsole`, `virusIntegrationTest`, `osirusJucePlugin`, `osTIrusJucePlugin` |
| **Waldorf** | `wLib`, `mqLib`, `mqJucePlugin`, `mqConsoleLib`, `mqTestConsole`, `mqPerformanceTest`, `xtLib`, `xtJucePlugin`, `xtTestConsole` |
| **Clavia** | `nord/n2x/*` |
| **Roland** | `ronaldo/{common,esp,h8s,je8086}` |
| **Build glue** | `juce.cmake`, `skins.cmake`, `skins.h.in`, `macsetup.cmake`, `macsetup.command.in`, `exporttarget.cmake`, `changelog.cmake`, `findvst2.cmake`, `runAuValidation.cmake` |
| **Dead** | `mqVst2` (only a `.gitignore`), `Android` (only `.gitignore`s tracked; the app itself was never committed), `/azure-pipelines.yml` (unmodified "Hello, world" starter template) |
| **Stray, untracked** | `3rdparty/SDL`, `3rdparty/asmjit`, `3rdparty/portmidi-latest` — clean clones of upstream, left over from a previous branch checkout in this working copy; owned by the `vfx` / `jade` / `sxx330` branches, which carry their own copies |

The pipeline in `wt/` roughly doubles this: `q` adds `goldfingerJucePlugin`,
`vavraJucePlugin`, `qPerformanceTest`, `syx2bin`; `vfx` adds a whole `ensoniq/` tree
(9 dirs); `sn2` adds `nova/` + `h8sEmu`; `jade`/`sxx330` grow `ronaldo/` to 10 dirs.
On current trends `source/` reaches ~85 flat entries. That is the actual problem to
solve — not today's 60.

## 2. Specific problems

1. **No ownership signal.** `libresample`, `ptypes`, `portaudio` and `fst` sit next to
   `synthLib` and `baseLib` as equals. A newcomer cannot tell what they may edit.
2. **`3rdparty/` is a lie.** Four of the six 3rd-party submodules (`JUCE`,
   `cpp-terminal`, `clap-juce-extensions`) and seven vendored copies live *outside* it.
3. **Manufacturer grouping is half-done.** Roland and Clavia are grouped; Access and
   Waldorf are not, despite having more directories each.
4. **Two different nesting conventions.** `ronaldo/je8086/jeLib` (3 levels) vs
   `virusLib` (1 level), for the same kind of thing.
5. **Cross-cutting CPU cores are filed under a manufacturer.** `ronaldo/h8s` is the
   Hitachi H8S core — the Supernova II needs it too, which is why `device/nova` grew a
   second copy at `source/h8sEmu`. That duplication is a direct symptom of the layout.
6. **Trademarked directory names.** `nord/` is the one that stands out (Nord is the
   product brand); `virus*`, `mq*`, `xt*` are the same issue one level down.

## 3. Proposed structure

```
source/
├── cmake/                    all build glue (juce, skins, macsetup, base, xcodeversion, …)
│
├── 3rdparty/                 everything not written by us
│   ├── README.md             ← upstream URL / fork URL / why patched / how to update
│   ├── JUCE/  RmlUi/  clap-juce-extensions/  cpp-terminal/     (submodule, our fork)
│   ├── freetype/  lunasvg/                                     (submodule, pristine)
│   └── fst/  libresample/  portaudio/  portmidi/  ptypes/  lua/  vstsdk2.4.2/   (vendored)
│
├── cpu/                      processor + DSP cores — silicon, not synths
│   ├── dsp56300/             (submodule, ours)
│   ├── mc68k/                (submodule, ours)
│   └── h8s/                  ← from ronaldo/h8s, merged with device/nova's h8sEmu
│
├── framework/                everything synth-agnostic
│   ├── baseLib/  synthLib/  hardwareLib/  networkLib/
│   ├── juce/                 jucePluginLib, jucePluginEditorLib, juceRmlUi,
│   │                         juceRmlPlugin, juceUiLib, jucePluginData, mcpServerLib
│   └── tools/                bridge, pluginTester, midiLearnTest, changelogGenerator
│
├── axel/                     Access
│   └── virus/                virusLib, virusJucePlugin, virusConsoleLib,
│                             virusTestConsole, virusIntegrationTest,
│                             osirusJucePlugin, osTIrusJucePlugin
├── waldi/                    Waldorf
│   ├── common/               wLib
│   ├── microq/               mqLib, mqJucePlugin, mqConsoleLib, mqTestConsole, mqPerformanceTest
│   ├── xt/                   xtLib, xtJucePlugin, xtTestConsole
│   └── q/                    (from wt/q)
├── claudia/                  Clavia  (was nord/)
│   └── n2x/                  n2xLib, n2xJucePlugin, n2xTestConsole
├── ronaldo/                  Roland  (unchanged, minus h8s → cpu/)
│   ├── common/  esp/  csp/  v55pi/  h8500/
│   └── je8086/  jade/  se70/  sxx330/
├── novak/                    Novation  (from wt/sn2)
│   └── nova/
└── enzo/                     Ensoniq  (from wt/vfx)
    ├── common/
    └── vfx/  ts10/
```

The rule, which `ronaldo/` already follows and everything else adopts:

> `<maker>/<family>/<target-dir>`, plus `<maker>/common/` for code shared across that
> maker's families and `<maker>/<chip>/` for that maker's custom silicon (`esp`, `csp`,
> `es5510`). Generic CPUs used by more than one maker go to `cpu/`.

`source/` drops from 60 entries to **11**.

## 4. Recommendation on the forked 3rd-party code

**Keep the forks in `3rdparty/`. Do not create a separate `forks/` folder.**

Reasoning:

- The folder split would encode *authorship of the patches*, which nobody navigating the
  tree needs. What people actually need to know is *"may I edit this?"* — and the answer
  for every entry in `3rdparty/` is the same: no, not directly; patch it upstream in our
  fork and bump the submodule. One folder, one rule.
- The category is not stable. A pristine dep gets one patch and has to migrate folders;
  a fork gets upstreamed and migrates back. Every migration is a submodule path change,
  a `.gitmodules` edit and a merge conflict for every open branch. That is churn for
  information a text file carries for free.
- The distinction that *is* mechanically real — "can I `git submodule update` this and
  be done" vs "this is a frozen copy with no upstream" — is already visible in
  `.gitmodules`, and reinforced by the README table below.

So: one `source/3rdparty/README.md`, one table, no new folders:

| Directory | Upstream | Our fork | Patched because | Update procedure |
|---|---|---|---|---|
| `JUCE` | juce-framework/JUCE | dsp56300/JUCE | … | rebase fork, bump submodule |
| `RmlUi` | mikke89/RmlUi | dsp56300/RmlUi | … | rebase fork, bump submodule |
| `clap-juce-extensions` | free-audio/… | dsp56300/… | … | rebase fork, bump submodule |
| `cpp-terminal` | jupyter-xeus/… | dsp56300/… | … | rebase fork, bump submodule |
| `freetype`, `lunasvg` | upstream | — | not patched | bump submodule |
| `fst`, `libresample`, `portaudio`, `portmidi`, `ptypes`, `lua` | upstream | — | **vendored, diverged, frozen** | manual re-vendor, expect conflicts |

One caveat worth acting on separately: `libresample`, `ptypes` and `portmidi` are frozen
copies we have effectively adopted. If any of them is ever *rewritten* rather than
patched, that copy stops being 3rd party and should move into `framework/` under its own
name and licence header. That is a per-library judgement call, not a structural one, and
none of them qualifies today.

## 5. Naming

`ronaldo` sets the convention: **a plausible personal first name that echoes the brand**.
Carrying it through:

| Manufacturer | Directory | Note |
|---|---|---|
| Roland | `ronaldo` | existing |
| Access | `axel` | German first name, German company |
| Waldorf | `waldi` | German company; `waldemar` if you prefer a full name |
| Clavia | `claudia` | replaces `nord/`, which is the actual product trademark |
| Ensoniq | `enzo` | |
| Novation | `novak` | replaces `nova/`, which is a Novation product line |

Scope limit: **rename the manufacturer folders only.** Leave `virusLib`, `mqLib`,
`xtLib`, `n2xLib`, `jeLib` and their CMake targets and C++ namespaces alone in this pass.
Renaming those touches ~700 `#include` lines and every `target_link_libraries` — a
separate, opt-in change, and one that should be argued on its own merits rather than
smuggled into a move.

## 6. Why this is cheap — the key finding

Every one of our libraries exports its include root as
`target_include_directories(<lib> PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)` — verified in
all 23 of them, including the already-nested `n2xLib`, `jeLib` and `esp`. The path is
relative to the library's *own* directory, so it follows the library when it moves:
`framework/synthLib` exports `framework/`, and consumers keep writing
`#include "synthLib/device.h"` unchanged.

Consequences:

- **~2900 `#include` lines across the repo need zero edits.** Nesting depth is free.
- CI, Jenkins and the deploy scripts reference the *build* directory, not source paths —
  `git grep` finds exactly one `source/…` mention in the whole of `scripts/`,
  `deploy/`, `installer/` and `.github/`, and it is inside a comment.
- `skins.cmake`, `juce.cmake` and `products.cmake` are all path-relative or
  `CMAKE_SOURCE_DIR`-rooted. No changes.

## 7. What actually breaks

A bounded list of **~12 lines**, all found by `git grep '\.\./'` over our CMakeLists:

| File(s) | Line | Fix |
|---|---|---|
| ~~8 × `target_include_directories(… ../JUCE/modules)`~~ | — | **done in Phase 1**: 6 deleted as redundant, 2 now use `${JUCE_MODULES_DIR}`. Phase 2 edits one line. |
| `mqTestConsole/CMakeLists.txt:12` | `../portmidi/pm_common/portmidi.h` | drop the header from `SOURCES`, or use the portmidi target's interface |
| `findvst2.cmake:1` | `../scripts/rclone.cmake` | repath |
| `virusIntegrationTest/runTest.cmake:1` | `../../scripts/rclone.cmake` | repath |
| `bridge/*`, `networkLib`, `ptypes` | `target_include_directories(… ../)` | already relative to own dir — no change |

Plus the mechanical work: `.gitmodules` paths for the 5 relocated submodules (`git mv`
rewrites these automatically on git ≥ 2.9; we are on 2.42 — verify then
`git submodule sync`), and the `add_subdirectory()` list in `source/CMakeLists.txt`,
which splits into one CMakeLists per new folder.

## 8. Migration plan

**Order matters more than the moves.** There are 14 live worktrees, several with large
uncommitted or unpushed arcs (`sn2mc`, `q`, `vfx`, `jade`). A tree-wide move made before
those land turns every one of them into a rename-conflict exercise.

**Phase 0 — prerequisites (do first, separately)**
- Land or park the in-flight branches you care about. At minimum `device/nova`,
  `device/nova-mc`, `device/q` should be pushed to their remotes before Phase 2.
- `git config rerere.enabled true` on every worktree, so a conflict resolved once during
  rebase is not re-resolved per branch.

**Phase 1 — free cleanup (no moves, land immediately)** — ✅ **done, uncommitted**
- ✅ Deleted `source/mqVst2/` (a lone `.gitignore`, referenced nowhere) and
  `/azure-pipelines.yml` (the unmodified starter template, referenced nowhere).
- ✅ Added `/wt/` to `.gitignore`.
- ✅ The 8 `../JUCE/modules` lines: **6 were redundant, not mis-pathed.** `juceUiLib` and
  `juceRmlUi` are the only two targets that need the module headers directly; every other
  consumer links one of them `PUBLIC` and inherits the path. Proof: `n2xJucePlugin` and
  `jeJucePlugin` sit at depth 3, where `../JUCE/modules` cannot resolve, and build fine.
  So: 6 lines deleted, 2 rewritten to `${JUCE_MODULES_DIR}`, set once in
  `source/CMakeLists.txt`. Phase 2 now moves JUCE by editing **one** line.
- ⏸ `source/Android/` — **left alone, needs your decision.** What is on disk is
  `.gradle/`, `.idea/`, `app/bin`, `app/build`, `gradle/` and `local.properties`: build
  output and IDE state only. There is no `build.gradle`, `settings.gradle`,
  `AndroidManifest.xml` or `src/` anywhere, tracked or untracked — the Android Studio
  project the tracked `.gitignore` describes no longer exists. The actual Android build
  does not use this directory at all: `build_android_abi.bat` drives CMake with the NDK
  toolchain straight into `temp/cmake_android_<abi>`. So this looks deletable, but it is
  your call, and the stale artifacts date from March 2025.
- ⏸ `3rdparty/{SDL,asmjit,portmidi-latest}` — **left in place deliberately.** All three
  are clean clones of their public upstreams with zero local modifications, and the
  branches that need them (`vfx`, `jade`, `sxx330`) carry their own copies inside their
  worktrees. The copies here are leftovers from a previous branch checkout in this working
  copy. Deleting them buys three lines of `git status` quiet and costs a multi-hundred-MB
  re-clone next time one of those branches is checked out here. Not worth it.
  The real bug behind them — those branches commit the paths as gitlinks with no matching
  `.gitmodules` entry — should be fixed on those branches, not here.

**Phase 2 — the move (one commit, pure `git mv` + CMake path edits, no content changes)**

Keeping it to a single mechanical commit is what makes rename detection work, and lets
every branch rebase across it in one step.

1. `source/cmake/` — move the 9 glue files + root `base.cmake`, `xcodeversion.cmake`.
2. `source/3rdparty/` — move `JUCE`, `cpp-terminal`, `clap-juce-extensions`, `fst`,
   `libresample`, `portaudio`, `portmidi`, `ptypes`, `vstsdk2.4.2` in. Write the README.
3. `source/cpu/` — move `dsp56300`, `mc68k`, `ronaldo/h8s`.
4. `source/framework/` — move the 15 framework dirs into `framework/`, `framework/juce/`,
   `framework/tools/`.
5. `source/axel/virus/`, `source/waldi/*`, rename `nord` → `claudia`.
6. Split `source/CMakeLists.txt` into per-folder CMakeLists; fix the ~12 lines from §7.
7. Configure + build Debug on Windows, Linux, macOS. `ctest -C Release` for the virus
   integration tests. No behaviour changed, so a green build *is* the verification.

**Phase 3 — rebase the worktrees**, one at a time, `q` and `vfx` first (biggest, most
new files). Each branch's new directories get placed under the new scheme as part of its
own rebase — `enzo/` and `novak/` come into existence there, not in Phase 2.

**Phase 4 — docs.** `CLAUDE.md` (the per-synth table and "Where to Make Changes"),
`.github/copilot-instructions.md`, `README.md`.

## 9. Deliberately not in scope

- Renaming libs / CMake targets / C++ namespaces (`virusLib` → `axelLib` etc.). Huge
  diff, separate decision. §5.
- Splitting `synthLib` or `jucePluginLib`. They are large but cohesive; no evidence of a
  seam worth cutting.
- Converting vendored copies to submodules or `FetchContent`. Would make the build
  network-dependent; current setup works.
- A `source/tests/` tree. Tests live next to what they test and that is fine.
