#!/usr/bin/env python3
"""Measure DSP56300 emulation throughput (MIPS) of virusTestConsole across git tags.

Rebuilds each tag in a dedicated worktree and runs it on the local Windows machine
and, optionally, on a remote Linux box over ssh (plink). Results are appended to a
CSV as they are produced, so the job is resumable and survives an abort: any
(tag, CPU) pair that already has a MIPS value is skipped on the next invocation.

See doc/dsp_performance_history.md for what the numbers mean and how to extend the
tag list. That document also records the traps this script works around - do not
"simplify" the pty, the affinity pinning, the warm-up origin or the wav check away
without reading it first.

Usage:
    python scripts/perfhist.py                  # every tag in TAGS, resuming
    python scripts/perfhist.py 2.2.9 HEAD       # just these tags

Requires (Windows host): CMake, Visual Studio, and `pip install pywinpty`.
The pty is not optional - see the module docstring in doc/.
"""
import csv, ctypes, os, re, shutil, statistics, subprocess, sys, threading, time

# ---------------------------------------------------------------- configuration
# Everything machine-specific lives here and can be overridden with env vars.

SCRIPTS = os.path.dirname(os.path.abspath(__file__))
REPO    = os.path.dirname(SCRIPTS)

# Scratch space for build dirs, run dirs and per-tag build logs. Keep this OUT of
# the repo: it is large, churns constantly, and one build dir is wiped per tag.
WORK    = os.environ.get("PERFHIST_WORK") or os.path.join(
              os.environ.get("TEMP", "/tmp"), "perfhist")

WT      = os.environ.get("PERFHIST_WORKTREE") or os.path.join(REPO, "wt", "perfhist")
BUILD   = os.path.join(WORK, "build_win")
RUNDIR  = os.path.join(WORK, "run_win")
LOGS    = os.path.join(WORK, "logs")
CSVOUT  = os.environ.get("PERFHIST_CSV") or os.path.join(
              REPO, "doc", "dsp_performance_history.csv")

# The ROM the emulator boots. Must be a Virus ABC ROM; the run directory is kept
# clean of every other .bin so ROMLoader::findROM("") resolves to exactly this one.
ROM_SRC = os.environ.get("PERFHIST_ROM") or os.path.join(
              os.path.expanduser("~"), "Documents", "The Usual Suspects", "Osirus",
              "roms", "Virus_C_OS_Flash_V6_5.BIN")
ROM     = os.path.basename(ROM_SRC)

# Remote Linux machine. Set PERFHIST_REMOTE="" to measure the Windows host only.
PLINK   = os.environ.get("PERFHIST_PLINK") or r"C:\Apps\putty\plink.exe"
PI      = os.environ.get("PERFHIST_REMOTE", "root@devpi5")
PI_SRC  = os.environ.get("PERFHIST_REMOTE_SRC", "/root/VirusEmulator/wt/perfhist")
PI_BLD  = os.environ.get("PERFHIST_REMOTE_BUILD", "/root/build/perfhist")
PI_RUN  = os.environ.get("PERFHIST_REMOTE_RUN", "/root/perfrun")
PI_CORES = os.environ.get("PERFHIST_REMOTE_CORES", "4-7")   # taskset core list

PRESETS = ["Impact  MS", "IndiArp BC"]
RUNS    = 3
WIN_DUR, WIN_WARM = 45, 15
PI_DUR,  PI_WARM  = 90, 30

CPU_WIN = os.environ.get("PERFHIST_CPU_WIN", "Ryzen 9 7950X3D")
CPU_PI  = os.environ.get("PERFHIST_CPU_REMOTE", "Cortex-A76 2.4GHz")

# A 7950X3D has two dissimilar CCDs and unpinned runs migrate between them mid-run,
# swinging results ~9% and looking exactly like thermal drift. 0xFFFF0000 = logical
# 16-31 = the non-V-Cache CCD, measured fastest and flat over a 130 s capture.
# Set to 0 to disable pinning on a uniform CPU.
WIN_AFFINITY = int(os.environ.get("PERFHIST_WIN_AFFINITY", "0xFFFF0000"), 0)

# Pin the MSVC toolset so the curve reflects emulator changes, not compiler vintage.
# Without this, tags before 2.2.x silently pick up whatever the newest toolset is
# while 2.2.x forces v143 in its own CMakeLists.
WIN_GENERATOR = os.environ.get("PERFHIST_GENERATOR", "Visual Studio 18 2026")
WIN_TOOLSET   = os.environ.get("PERFHIST_TOOLSET", "v143")

CMAKE_COMMON = ["-Dgearmulator_BUILD_JUCEPLUGIN=OFF",
                # source/portaudio still declares cmake_minimum_required(2.8),
                # which CMake >= 4.0 rejects outright
                "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]

# Newest first, so the most relevant half lands early and old-tag build rot cannot
# block it. Same-week duplicate releases are omitted; add tags here to extend.
TAGS = ["HEAD", "2.2.9", "2.2.6", "2.2.3", "2.2.2", "2.1.4", "2.1.0", "2.0.14",
        "2.0.12", "2.0.9", "2.0.0", "1.4.4", "1.4.2", "1.4.1", "1.4.0", "1.3.21",
        "1.3.20", "1.3.17", "1.3.15", "1.3.14", "OsTIrus_1.3.12", "Osirus_1.3.6",
        "Osirus_1.3.3_DonatorsAlpha", "osirus_1.2.30", "1.2.29", "1.2.25",
        "1.2.22_openBeta", "waldorf_ship00", "1.2.15", "1.2.5_open_beta",
        "1.1.9_open_beta", "0.0.2_closed_beta"]

COLUMNS = ["Date", "Tag", "Commit", "ROM", "Preset", "CPU", "MIPS", "Notes",
           "StdDev", "Samples", "RunMedians"]


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def sh(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, errors="replace", **kw)


def plink(cmd, timeout=3600):
    return sh([PLINK, "-batch", PI, cmd], timeout=timeout)


def write_log(name, text):
    with open(os.path.join(LOGS, name), "w", encoding="utf-8", errors="replace") as f:
        f.write(text)


def tail(text, n=25):
    return " | ".join(l.strip() for l in text.strip().splitlines()[-n:] if l.strip())[:800]


# ---------------------------------------------------------------- checkout

def local_modules():
    """path/basename -> local submodule gitdir, so relative ../foo URLs can be
    resolved offline against the objects this clone already has."""
    base = os.path.join(REPO, ".git", "modules")
    m = {}
    for root, dirs, files in os.walk(base):
        if "config" in files and "objects" in dirs:
            rel = os.path.relpath(root, base).replace("\\", "/")
            m[rel] = root.replace("\\", "/")
            m.setdefault(os.path.basename(rel), root.replace("\\", "/"))
            dirs[:] = [d for d in dirs if d not in ("objects", "refs", "logs", "info")]
    return m


MODULES = None


def serve_local_stores(enable):
    """Old tags pin submodule commits that are reachable but are not branch tips, and
    upload-pack refuses `want <sha>` for those. The flag has to live in the served
    repo (-c does not reach a local upload-pack), so set it and put it back after."""
    for path in set(local_modules().values()):
        if enable:
            sh(["git", "-C", path, "config", "uploadpack.allowAnySHA1InWant", "true"])
        else:
            sh(["git", "-C", path, "config", "--unset", "uploadpack.allowAnySHA1InWant"])


def url_overrides():
    """Relative submodule URLs (../dsp56300) resolve against the superproject's own
    path, which git >= 2.38 refuses to fetch over the file transport. Return -c
    overrides pointing them at the gitdirs in .git/modules - every commit we need is
    already there, so this stays offline.

    Overrides rather than config writes on purpose: a worktree SHARES .git/config
    with the main checkout, so `git submodule sync` here would rewrite the real
    submodule URLs in the developer's live repo."""
    gm = os.path.join(WT, ".gitmodules")
    if not os.path.exists(gm):
        return []
    args = []
    for line in sh(["git", "config", "-f", gm, "--get-regexp",
                    r"submodule\..*\.url"]).stdout.splitlines():
        key, _, url = line.partition(" ")
        if "://" in url or url.startswith("git@"):
            continue
        name = key[len("submodule."):-len(".url")]
        local = MODULES.get(name) or MODULES.get(
            os.path.basename(url.lstrip("./").removesuffix(".git")))
        if local:
            args += ["-c", f"submodule.{name}.url={local}"]
    return args


# Missing transitive includes that current MSVC/GCC headers no longer provide.
# Declarations only - they cannot change generated code, so measurements stay
# comparable. Anything beyond this is left alone unless listed further down.
PATCHES = [
    ("source/dsp56300/source/dsp56kEmu/dspthread.cpp", "<chrono>", "#include <chrono>"),
    ("source/dsp56300/source/dsp56kEmu/opcodeinfo.h", "<cstddef>",
     "#include <cstddef>\n#include <cstdint>"),
    ("source/baseLib/filesystem.h", "<array>", "#include <array>"),
]


def disable_trace():
    """<= 1.2.15 ships the test console with dsp.enableTrace(Ops|Regs|StackIndent)
    left switched on. Every later version has this line commented out; do the same,
    so the number means what the rest of the column means."""
    applied = []
    for rel in ("source/virusTestConsole/virusTestConsole.cpp",
                "source/virusConsoleLib/consoleApp.cpp"):
        path = os.path.join(WT, *rel.split("/"))
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        hit = False
        for i, l in enumerate(lines):
            if "enableTrace(" in l and not l.lstrip().startswith("//"):
                lines[i] = l.replace(l.lstrip(), "// " + l.lstrip(), 1)
                hit = True
        if hit:
            with open(path, "w", encoding="utf-8", errors="replace") as f:
                f.writelines(lines)
            applied.append(os.path.basename(rel) + ":enableTrace off")
    return applied


def fix_sfinae_ambiguity():
    """<= osirus_1.2.30 will not compile with MSVC 14.44: readMem<Inst, MMM> and
    writeMem<Inst, MMM> are ambiguous because MMM == 0 also binds the sibling
    2-parameter overload's trailing `enable_if<...>::type*`. Move that sibling's
    SFINAE into its return type, so it takes one template parameter and can no
    longer match an explicit MMM.

    Only overloads that actually have an explicit-MMM twin with an identical
    condition are touched, so this cannot silently reroute anything else - and it
    selects exactly the overload GCC already selects (the Linux box compiles these
    tags unpatched), which keeps the two columns comparable. Validate with the
    x64/ARM ratio after any change here; see the doc."""
    def sfinae(cond, ret):
        return f"typename std::enable_if<{cond}{'' if ret == 'void' else ', ' + ret}>::type"

    applied = []
    for rel, decl3, decl2 in (
        ("source/dsp56300/source/dsp56kEmu/dsp.h",
         r"template\s*<Instruction Inst,\s*TWord MMM,\s*typename std::enable_if<"
         r"(?P<cond>.+?)>::type\*\s*=\s*nullptr>\s*\n[ \t]*"
         r"(?P<ret>[\w:]+)\s+(?P<name>\w+)\(",
         r"template\s*<Instruction Inst,\s*typename std::enable_if<{cond}>::type\*"
         r"\s*=\s*nullptr>\s*\n(?P<ind>[ \t]*){ret}\s+{name}\((?P<args>[^;)]*)\)"
         r"(?P<cv>\s*const)?;"),
        ("source/dsp56300/source/dsp56kEmu/dsp_ops_helper.inl",
         r"template\s*<Instruction Inst,\s*TWord MMM,\s*typename std::enable_if<"
         r"(?P<cond>.+?)>::type\*>\s*(?P<ret>[\w:]+)\s+DSP::(?P<name>\w+)\(",
         r"(?P<ind>)template\s*<Instruction Inst,\s*typename std::enable_if<{cond}>::type\*>"
         r"\s*{ret}\s+DSP::{name}\((?P<args>[^;)]*)\)(?P<cv>\s*const)?"),
    ):
        path = os.path.join(WT, *rel.split("/"))
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        n = 0
        for m in re.finditer(decl3, text):
            cond, ret, name = m.group("cond"), m.group("ret"), m.group("name")
            pat = decl2.format(cond=re.escape(cond), ret=re.escape(ret),
                               name=re.escape(name))
            semi = ";" if rel.endswith(".h") else ""

            def rep(mm, cond=cond, ret=ret, name=name, semi=semi):
                return (f"template <Instruction Inst>\n{mm.group('ind')}"
                        f"{sfinae(cond, ret)} "
                        f"{'' if semi else 'DSP::'}{name}({mm.group('args')})"
                        f"{mm.group('cv') or ''}{semi}")
            text, k = re.subn(pat, rep, text)
            n += k
        if n:
            with open(path, "w", encoding="utf-8", errors="replace") as f:
                f.write(text)
            applied.append(f"{os.path.basename(rel)}:sfinae x{n}")
    return applied


def apply_patches():
    applied = disable_trace() + fix_sfinae_ambiguity()
    for rel, marker, insert in PATCHES:
        path = os.path.join(WT, *rel.split("/"))
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        if marker in text:
            continue
        lines = text.splitlines(keepends=True)
        at = next((i + 1 for i, l in enumerate(lines)
                   if l.startswith("#include") or l.startswith("#pragma once")), 0)
        lines.insert(at, insert + "\n")
        with open(path, "w", encoding="utf-8", errors="replace") as f:
            f.write("".join(lines))
        applied.append(os.path.basename(rel))
    return applied


def checkout(tag):
    global MODULES
    if MODULES is None:
        MODULES = local_modules()
    ref = sh(["git", "-C", REPO, "rev-parse", "HEAD"]).stdout.strip() \
        if tag == "HEAD" else tag
    r = sh(["git", "-C", WT, "checkout", "--detach", "--force", ref])
    if r.returncode:
        return None, None, None, tail(r.stderr)
    # deliberately no `submodule sync` - see url_overrides()
    upd = ["git", "-c", "protocol.file.allow=always"] + url_overrides() + \
          ["-C", WT, "submodule", "update", "--init", "--recursive", "--force"]
    r = sh(upd, timeout=1800)
    for _ in range(3):
        if not r.returncode:
            break
        # Some pinned submodule commits never reached the public remote, so an
        # already-initialised submodule cannot fetch them by sha (GitHub answers
        # "not our ref"). Pull the refs from this clone's own store, then retry.
        m = re.search(r"submodule path '([^']+)'", r.stderr)
        store = MODULES.get(m.group(1)) if m else None
        if not store:
            break
        sh(["git", "-c", "protocol.file.allow=always", "-C",
            os.path.join(WT, *m.group(1).split("/")), "fetch", "--force", store,
            "+refs/heads/*:refs/remotes/perfhiststore/*"], timeout=900)
        r = sh(upd, timeout=1800)
    if r.returncode:
        return None, None, None, "submodule: " + tail(r.stderr)
    patched = apply_patches()
    sha = sh(["git", "-C", WT, "rev-parse", "--short", "HEAD"]).stdout.strip()
    date = sh(["git", "-C", WT, "log", "-1", "--format=%cd", "--date=short"]).stdout.strip()
    return sha, date, patched, None


# ---------------------------------------------------------------- builds

def build_win(tag):
    shutil.rmtree(BUILD, ignore_errors=True)
    cfg = sh(["cmake", "-S", WT, "-B", BUILD, "-G", WIN_GENERATOR,
              "-T", WIN_TOOLSET] + CMAKE_COMMON, timeout=1800)
    if cfg.returncode:
        write_log(f"{tag}_win_configure.log", cfg.stdout + cfg.stderr)
        return None, "win configure failed: " + tail(cfg.stdout + cfg.stderr)
    bld = sh(["cmake", "--build", BUILD, "--config", "Release",
              "--target", "virusTestConsole", "-j", "16"], timeout=3600)
    write_log(f"{tag}_win_build.log", bld.stdout + bld.stderr)
    if bld.returncode:
        return None, "win build failed: " + tail(bld.stdout + bld.stderr)
    # pre-1.2.29 tags declare the target in the root CMakeLists, so the exe lands
    # in <build>/Release instead of <build>/source/virusTestConsole/Release
    exe = next((os.path.join(r, "virusTestConsole.exe")
                for r, _, fs in os.walk(BUILD) if "virusTestConsole.exe" in fs), None)
    if not exe:
        return None, "win build produced no exe"
    for f in os.listdir(RUNDIR):
        if f.endswith(".wav"):
            os.remove(os.path.join(RUNDIR, f))
    dst = os.path.join(RUNDIR, "virusTestConsole.exe")
    shutil.copyfile(exe, dst)
    return dst, None


def build_pi(tag):
    cmd = (f"rm -rf {PI_BLD} && cmake -S {PI_SRC} -B {PI_BLD} -G 'Unix Makefiles' "
           f"-DCMAKE_BUILD_TYPE=Release " + " ".join(CMAKE_COMMON) +
           f" && cmake --build {PI_BLD} --target virusTestConsole -j 4"
           # old tags put the target in the root CMakeLists -> different path
           f" && cp \"$(find {PI_BLD} -type f -name virusTestConsole | head -1)\" {PI_RUN}/")
    r = plink(cmd, timeout=5400)
    write_log(f"{tag}_pi_build.log", r.stdout + r.stderr)
    if r.returncode:
        return False, "pi build failed: " + tail(r.stdout + r.stderr)
    return True, None


# ---------------------------------------------------------------- runs

MIPS_RE = re.compile(r"MIPS:\s*([0-9]+\.?[0-9]*)")
MIN_WAV = 1 << 20   # a run that never rendered audio has nothing worth timing

# 0.0.2 predates preset-by-name: it is `virusTestConsole <rom> <flat index>`, where
# index = bank*128 + preset. Bank/preset come from the source's own comments
# (loadSingle(v, 3, 56) // Impact  MS, loadSingle(v, 0, 51) // IndiArp BC).
# Passing a preset NAME there pops a modal message box and measures an idle DSP.
PRESET_INDEX = {"Impact  MS": 3 * 128 + 56, "IndiArp BC": 0 * 128 + 51}
CURRENT_TAG = None


def cli_args(preset):
    if CURRENT_TAG == "0.0.2_closed_beta":
        return [ROM, str(PRESET_INDEX[preset])]
    return [preset]


def trim(samples, warm):
    """Warm-up is counted from the FIRST MIPS line, not from process start: old tags
    spend up to a minute in JitUnittests (which on Linux logs its generated asm to
    stdout) before emulation begins, and a fixed offset lands inside that phase."""
    if not samples:
        return []
    t0 = samples[0][0]
    return [v for t, v in samples if t - t0 >= warm] or [v for _, v in samples]


def run_win(exe, preset, dur, warm):
    # A pty, not a pipe: the statically linked CRT block-buffers puts() at 4 KB,
    # which is ~62 s of MIPS output, and TerminateProcess/Ctrl-Break never flush it.
    import winpty
    p = winpty.PtyProcess.spawn([exe] + cli_args(preset), cwd=RUNDIR,
                                dimensions=(24, 200))
    if WIN_AFFINITY:
        h = ctypes.windll.kernel32.OpenProcess(0x0200 | 0x0400, False, p.pid)
        if h:
            ctypes.windll.kernel32.SetProcessAffinityMask(h, ctypes.c_size_t(WIN_AFFINITY))
            ctypes.windll.kernel32.CloseHandle(h)
    buf, t0, samples = "", time.time(), []
    while p.isalive() and time.time() - t0 < dur + warm:
        try:
            buf += p.read(4096)
        except EOFError:
            break
        while "\n" in buf:
            line, buf = buf.split("\n", 1)
            m = MIPS_RE.search(line)
            if m:
                samples.append((time.time(), float(m.group(1))))
    try:
        p.terminate(force=True)
    except Exception:
        pass
    audio = 0
    for f in os.listdir(RUNDIR):
        if f.endswith(".wav"):
            path = os.path.join(RUNDIR, f)
            audio = max(audio, os.path.getsize(path))
            try:
                os.remove(path)
            except OSError:
                pass
    return trim(samples, warm) if audio >= MIN_WAV else []


def run_pi(preset, dur, warm):
    piargs = " ".join(f"'{a}'" for a in cli_args(preset))
    # grep -a MIPS at the far end: old tags dump the whole JitUnittests disassembly
    # to stdout, and shipping that over ssh distorts what is being measured
    cmd = (f"cd {PI_RUN} && rm -f *.wav && timeout {dur + warm} taskset -c {PI_CORES} "
           f"stdbuf -oL ./virusTestConsole {piargs} 2>&1 | stdbuf -oL grep -a 'MIPS:'; "
           f"echo WAVBYTES=$(stat -c %s *.wav 2>/dev/null | sort -n | tail -1); rm -f *.wav")
    p = subprocess.Popen([PLINK, "-batch", PI, cmd], stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True, errors="replace",
                         bufsize=1)
    t0, samples, audio = time.time(), [], 0
    for line in p.stdout:
        if line.startswith("WAVBYTES="):
            audio = int(line.split("=", 1)[1].strip() or 0)
            continue
        m = MIPS_RE.search(line)
        if m:
            samples.append((time.time(), float(m.group(1))))
        if time.time() - t0 > dur + warm + 120:
            break
    try:
        p.wait(timeout=90)
    except subprocess.TimeoutExpired:
        p.kill()
    return trim(samples, warm) if audio >= MIN_WAV else []


def measure(runner, preset, dur, warm, label):
    """RUNS runs -> median of the run medians. Returns (mips, stdev, n, "a|b|c")."""
    meds, pooled = [], []
    for i in range(RUNS):
        s = runner(preset, dur, warm)
        if not s:
            log(f"    {label} '{preset}' run {i+1}: NO SAMPLES")
            continue
        meds.append(statistics.median(s))
        pooled += s
        log(f"    {label} '{preset}' run {i+1}: n={len(s)} median={meds[-1]:.1f}")
    if not meds:
        return None
    return (statistics.median(meds),
            statistics.pstdev(pooled) if len(pooled) > 1 else 0.0,
            len(pooled),
            "|".join(f"{m:.1f}" for m in meds))


# ---------------------------------------------------------------- csv

def load_done():
    done = set()
    if os.path.exists(CSVOUT):
        with open(CSVOUT, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                if r["MIPS"]:            # rows without a value stay retryable
                    done.add((r["Tag"], r["CPU"]))
    return done


def append(rows):
    new = not os.path.exists(CSVOUT)
    os.makedirs(os.path.dirname(CSVOUT), exist_ok=True)
    with open(CSVOUT, "a", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, COLUMNS)
        if new:
            w.writeheader()
        for r in rows:
            w.writerow(r)
        f.flush()
        os.fsync(f.fileno())


def row(date, tag, sha, preset, cpu, res, notes):
    d = dict.fromkeys(COLUMNS, "")
    d.update(Date=date, Tag=tag, Commit=sha, ROM=ROM, Preset=preset, CPU=cpu, Notes=notes)
    if res:
        mips, sd, n, meds = res
        d.update(MIPS=f"{mips:.2f}", StdDev=f"{sd:.2f}", Samples=n, RunMedians=meds)
    return d


# ---------------------------------------------------------------- main

def stage_rom():
    os.makedirs(RUNDIR, exist_ok=True)
    dst = os.path.join(RUNDIR, ROM)
    if not os.path.exists(dst):
        shutil.copyfile(ROM_SRC, dst)
    if PI:
        if plink(f"test -f {PI_RUN}/{ROM}").returncode:
            plink(f"mkdir -p {PI_RUN}")
            sh([PLINK.replace("plink", "pscp"), "-batch", ROM_SRC, f"{PI}:{PI_RUN}/"],
               timeout=600)


def sweep(only, done):
    global CURRENT_TAG
    for tag in only:
        CURRENT_TAG = tag
        need_win = (tag, CPU_WIN) not in done
        need_pi = bool(PI) and (tag, CPU_PI) not in done
        if not need_win and not need_pi:
            log(f"=== {tag}: already done, skipping")
            continue
        log(f"=== {tag}: checkout")
        sha, date, patched, err = checkout(tag)
        if err:
            log(f"    checkout FAILED: {err}")
            append([row("", tag, "", "", c, None, err) for c, n in
                    ((CPU_WIN, need_win), (CPU_PI, need_pi)) if n])
            continue
        note = f"build-only patch: {', '.join(patched)}" if patched else ""
        log(f"    {sha} {date} -- building" + (f" ({note})" if note else ""))

        res = {"win": (None, "skipped"), "pi": (False, "skipped")}
        bt = []
        if need_win:
            bt.append(threading.Thread(target=lambda: res.update(win=build_win(tag))))
        if need_pi:
            bt.append(threading.Thread(target=lambda: res.update(pi=build_pi(tag))))
        for t in bt:
            t.start()
        for t in bt:
            t.join()
        exe, werr = res["win"]
        piok, perr = res["pi"]
        log(f"    build win={'ok' if exe else werr}  pi={'ok' if piok else perr}")

        out = {}

        def do_win():
            for preset in PRESETS:
                out[("w", preset)] = measure(
                    lambda p, d, wm: run_win(exe, p, d, wm), preset,
                    WIN_DUR, WIN_WARM, "win")

        def do_pi():
            for preset in PRESETS:
                out[("p", preset)] = measure(run_pi, preset, PI_DUR, PI_WARM, "pi")

        # both machines measure concurrently: they are independent boxes
        threads = ([threading.Thread(target=do_win)] if exe else []) + \
                  ([threading.Thread(target=do_pi)] if piok else [])
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        rows = []
        for preset in PRESETS:
            if need_win:
                r = out.get(("w", preset)) if exe else None
                rows.append(row(date, tag, sha, preset, CPU_WIN, r,
                                note if r else (werr or
                                "no valid measurement (emulator rendered no audio)")))
            if need_pi:
                r = out.get(("p", preset)) if piok else None
                rows.append(row(date, tag, sha, preset, CPU_PI, r,
                                note if r else (perr or
                                "no valid measurement (emulator rendered no audio)")))
        append(rows)
        log(f"=== {tag}: written {len(rows)} rows")


def main():
    if not os.path.exists(WT):
        sys.exit(f"worktree missing: {WT}\n"
                 f"create it with: git worktree add --detach {WT} HEAD")
    if not os.path.exists(ROM_SRC):
        sys.exit(f"ROM not found: {ROM_SRC}  (set PERFHIST_ROM)")
    os.makedirs(LOGS, exist_ok=True)
    stage_rom()
    done = load_done()
    serve_local_stores(True)
    try:
        sweep(sys.argv[1:] or TAGS, done)
    finally:
        serve_local_stores(False)
    log("ALL DONE")


if __name__ == "__main__":
    main()
