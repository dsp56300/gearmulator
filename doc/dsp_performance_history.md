# DSP56300 Performance History

Long-running record of `virusTestConsole` throughput across released tags, so the effect
of DSP emulation work is visible over time rather than guessed at.

- **Data:** `doc/dsp_performance_history.csv`
- **Harness:** `scripts/perfhist.py`
- **Metric:** MIPS as reported by `DSPThread::threadFunc` (`dsp56300/source/dsp56kEmu/dspthread.cpp`) —
  emulated DSP instructions per microsecond. The DSP thread runs unthrottled here, so this is
  pure JIT throughput, not an audio-latency or real-time-factor measurement. Higher is better.
- **Baseline established:** 2026-08-26 — 32 tags, 122 measurements, 2021-07 → 2026-08.

The current numbers are also rendered as a chart:
<https://claude.ai/code/artifact/d72bfe0d-d701-4add-a6ec-b789727b5835>

## What is measured

| | |
|---|---|
| ROM | `Virus_C_OS_Flash_V6_5.BIN` (Virus C OS 6.5) |
| Presets | `Impact  MS` (two spaces) and `IndiArp BC` — the pair the `deploy/win/*.bat` starters use |
| Windows | Ryzen 9 7950X3D, MSVC 14.44 pinned via `-T v143` |
| Linux | Cortex-A76 @ 2.4 GHz (devpi5, 4 cores online), GCC 11.4 |
| Per cell | median of 3 runs; each run is the median of its per-second MIPS samples |

Every tag is compiled with **today's** toolchain, deliberately: that isolates our emulator
changes from five years of compiler evolution. The consequence is that these figures do not
reproduce older hand-recorded ones taken on period compilers and different CPUs — treat this
as its own baseline, not a continuation of the old spreadsheet.

## Re-running it

```bash
git worktree add --detach wt/perfhist HEAD
pip install pywinpty                       # required, see "Traps" below
python scripts/perfhist.py                 # all tags, resuming
python scripts/perfhist.py 2.3.0 HEAD      # just these
```

The CSV is append-only and the run is resumable: any `(tag, CPU)` pair that already carries a
MIPS value is skipped, so an aborted sweep continues where it stopped. A cell that failed is
written with an empty `MIPS` and a reason in `Notes`, and stays retryable.

Budget roughly **11 minutes per tag** — both machines build and measure concurrently. Builds
are quick (25 s Windows, ~2 min on the A76); the runs dominate.

Configuration is env vars at the top of the script — `PERFHIST_ROM`, `PERFHIST_REMOTE`
(set empty to measure the Windows host only), `PERFHIST_WORK`, `PERFHIST_TOOLSET`,
`PERFHIST_WIN_AFFINITY`. Build dirs and per-tag build logs go to `PERFHIST_WORK`
(default `%TEMP%/perfhist`), never into the repo.

## Adding a new data point

Append the tag to `TAGS` in `scripts/perfhist.py` (newest first) and run it with that tag as
an argument. To re-measure something, delete its rows from the CSV and re-run — otherwise the
resume logic skips it.

**Keep the comparison honest.** If the measuring machine, its OS, the toolset or the run
protocol changes, the new rows are not comparable to the old ones. Either keep the old setup
for the rows you want to compare against, or re-measure a couple of existing tags on the new
setup and record both — the `CPU` column exists to keep those populations separate.

## Reading the numbers

Run-to-run spread is under 2%, so differences above ~3% are real and anything smaller is noise.
`RunMedians` keeps all three run medians per cell so any figure can be sanity-checked without
re-running.

The **x64-to-ARM ratio is the cross-check**: healthy tags sit between 2.79 and 3.25. A tag
outside that band means something is wrong with that measurement — a build that silently took
a slower code path, a machine that was busy, a patch that rerouted something. Two known and
understood exceptions: the 2022 tags where ARM had no JIT at all, and HEAD, where the current
x64 work has no ARM counterpart yet.

## Traps

These are all load-bearing. Removing any of them silently corrupts the numbers rather than
failing loudly.

**1 — Windows needs a pty, not a pipe.** The build links the static CRT, which block-buffers
`puts()` at 4 KB. At ~66 bytes per MIPS line that is one flush per ~62 s, so a 45 s run captures
nothing at all, and neither `TerminateProcess` nor Ctrl-Break flushes it on exit. The harness
runs the child under `pywinpty`. Linux is free: `m_logToStdout` is `#ifdef _WIN32`, but
`m_logToDebug` defaults true and `LOG` is `fputs(stdout)` there, so `stdbuf -oL` suffices.

**2 — Pin the CPU affinity.** The 7950X3D has two dissimilar CCDs and unpinned runs migrate
between them mid-run: CCD0 measures 697 MIPS, CCD1 762, and a drifting run looks exactly like
thermal throttling. Pinned to CCD1 (`0xFFFF0000`), a 130 s capture holds flat within ±1.7%.

**3 — Warm-up starts at the first MIPS line, not at process start.** Old tags spend up to a
minute in `JitUnittests`, which on Linux logs its entire generated disassembly to stdout. A
fixed offset from process start lands inside that phase and reports ~0.1 MIPS.

**4 — Discard any run that rendered no audio.** The harness requires a `.wav` of at least 1 MB
before it will believe a measurement. This is what catches a console that started but never
emulated: `0.0.2_closed_beta` takes `<romPath> <flatPresetIndex>` rather than a preset name, and
passing a name pops a modal message box while the DSP idles at a plausible-looking few MIPS.

**5 — Never run `git submodule sync` in the worktree.** A worktree *shares* `.git/config` with
the main checkout, so syncing rewrites the real submodule URLs in the live repo into
worktree-relative `../` paths. Pass `-c submodule.<name>.url=<path under .git/modules>` instead,
together with `-c protocol.file.allow=always` (git ≥ 2.38 refuses the file transport by default).
Some pinned `dsp56300` commits exist only on the stale private remote and GitHub answers
`not our ref` for them; the harness then fetches by refspec from this clone's own module store.

**6 — Pin the toolset.** Without `-T v143`, tags before 2.2.x silently pick the newest installed
MSVC while 2.2.x forces v143 in its own `CMakeLists.txt`, which makes the curve part emulator
and part compiler.

## Patches old tags need

The harness applies these automatically and records them in the CSV `Notes` column.

Missing includes that older standard libraries used to leak transitively — `<chrono>` in
`dspthread.cpp`, `<cstddef>`/`<cstdint>` in `opcodeinfo.h`, `<array>` in `baseLib/filesystem.h`.
These are declarations and cannot change generated code.

`≤ 1.2.15` ships `virusTestConsole.cpp` with `dsp.enableTrace(Ops|Regs|StackIndent)` left
switched on; every later version has it commented out, so the harness comments it out too.

**One real source change:** tags at or before `osirus_1.2.30` do not compile with MSVC 14.44.
`readMem<Inst, MMM>` and `writeMem<Inst, MMM>` are ambiguous because `MMM == 0` also binds the
sibling 2-parameter overload's trailing `enable_if<...>::type*`. The harness moves that
sibling's SFINAE into its return type so it takes one template parameter and can no longer
match an explicit `MMM`. Only overloads that genuinely have an explicit-`MMM` twin with an
identical condition are touched, and the result is the overload GCC already selects — the Linux
box compiles these tags unpatched. Verified by the x64/ARM ratio: every patched tag lands at
2.88–3.13, inside the band the unpatched tags occupy.

`/permissive` does not help here, and neither does an older toolset — 14.36 and 14.38 are
present in the VS install but have no `cl.exe`, and 14.42 fails identically.

## Known gaps

Six cells are blank, and all six are genuine rather than unfinished work:

| Tag | Missing | Why |
|---|---|---|
| `0.0.2_closed_beta` | aarch64 | x86-only compiler flags (`-msse`) |
| `1.1.9_open_beta` | aarch64 | `synthLib/audiobuffer.cpp` rejected by GCC 11 |
| `waldorf_ship00` | aarch64 | `JitEmitter` of that vintage has no ARM path |

`1.2.29` on Windows carries a second, unrelated conformance error that is deliberately left
unpatched; it keeps its Linux numbers.

## What the baseline showed

- **HEAD vs 2.2.9: +50% on x64** (989 → 1482 MIPS, `Impact  MS`) and **+21% on ARM** (348 → 421).
  The left-aligned ALU and JIT trampoline arc is by far the largest single step in the series.
- **2.0.0 → 2.2.9 is flat** at ~1000–1040 x64 and ~338–354 ARM. Eleven months of releases with
  no throughput movement.
- **The aarch64 JIT lands at `1.2.22_openBeta`**: 5.8 → 234 MIPS. Before it, the 2022 ARM builds
  fall back to the interpreter. That is a 40× step, not a curve — the chart breaks the line there
  rather than interpolating across it.
- Since the first closed beta, x64 throughput is up **4.6×** (324 → 1482 MIPS).
