#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""THE CANONICAL 36-SCENARIO CORPUS RUNNER — the arbiter for every before/after corpus arm.

★★ WHY THIS FILE EXISTS AT ALL. Until 2026-08-30 there was NO maintained corpus runner. The authority was an
ad-hoc shell loop, retyped per slice and archived inside an evidence document
(`docs/superpowers/evidence/2026-08-30-custody-slice-b-corpus.md:218`):

    for f in simulation/*.json; do b=$(basename $f .json); [ "$b" = topo_9node ] && continue
      .../lus -e meshroute "$f" before/$b.ndjson; done

That loop is a measurement instrument with no controls. Read it literally: **it never examines an exit status**,
so a scenario that died leaves whatever bytes it had already written under the FINAL name and the loop moves on;
its scenario set is whatever globbing returns, so `simulation/` gaining or losing a file silently resizes the
corpus; and `[ "$b" = topo_9node ]` is a hard-coded skip that no new stray file would ever match.

★★★ THE AUTHORITY CHAIN, AND WHY EACH LINK EXISTS (§GATE-SPEED QG round 1 found the first three MISSING, each a
    way to produce a GREEN comparison from work that never happened):

  ① FROZEN INPUTS -> ② VALIDATED PROMOTION -> ③ VALIDATED MANIFEST -> ④ VALIDATED COMPARE

  ① **The inputs are SNAPSHOTTED and executed FROM the snapshot.** `lus`, `BASELINE.md` and all 36 scenario JSONs
     are hashed, copied into `<out>/inputs/`, and the run executes the COPIES. Hashing inputs *after* the run — as
     this file did in round 1 — lets one "corpus" contain streams produced by two different `lus` binaries and
     still describe itself with one hash. The originals are re-hashed at the end and a change is a REFUSAL, since
     a corpus whose sources moved mid-run is not attributable to a tree. Cost: ~5.8 MB per run (lus 2.3 MB +
     scenarios 1.4 MB + BASELINE 2.1 MB) against ~244 MB of streams — 2.4 %, so the strong form is simply free.

  ② **A stream is promoted only after its RESULT is validated.** `lus` must exit 0, write the file, print EXACTLY
     ONE result line, and report ZERO assertion failures. Only then does `os.replace()` move it into `streams/`.
     Round 1 promoted first and recorded the failure afterwards, so an exit-0 run that printed no summary left a
     promoted stream in a "failed" run — a corpus half-anchored to nothing. Rejected output is kept under
     `<out>/rejected/` for diagnosis; `streams/` only ever holds validated streams.

  ③ **A manifest is only believed if it describes a complete, successful run** — 36 unique records, every one
     exit-0/promoted/failure-free/0-assertion-failures, every stream present on disk and REHASHING to its recorded
     hash, and no unexpected file in `streams/`. `validate_run()` is run at the END of a run and again by
     `--compare`, so the same authority judges both.

  ④ **`--compare` validates BOTH sides before comparing a single field.** Round 1 compared manifests as bare JSON:
     two identically FAILED runs compared equal and printed PASS — the worst false-green shape there is.

★ PARALLELISM IS A PROPERTY OF THIS RUNNER, NOT A BOLT-ON. `--jobs=1` and `--jobs=N` are THE SAME code path with a
different pool size; `--jobs=1` is therefore the sequential arbiter, and `--compare` is the gate that proves the
two agree. There is no second implementation to drift.

★ THE SCENARIO SET IS PARSED, NEVER HAND-LISTED. It is read from the canonical `### 36/36 corpus` block of
`simulation/BASELINE.md` — the owner-ruled anchor table. ⛔ THIS RUNNER ONLY *READS* THAT TABLE: it is triply
owner-ruled (2026-08-10 / 2026-08-29 / 2026-08-30) and no agent — this one included — edits it. A missing row, a
duplicate row, a scenario JSON that no row names, or a row whose JSON is absent ⇒ REFUSAL, never a partial pass.
The one non-scenario JSON in `simulation/` is `topo_9node.json` (a bare link-budget matrix, not a runnable
scenario — no `nodes`/`events`), named here explicitly so a NEW stray JSON is refused instead of skipped.

★ THE PARENT OWNS THE CHILDREN. Every `lus` runs in its OWN session/process group, so a terminal Ctrl-C does not
reach them through the shell's foreground group — this process is solely responsible for killing and reaping them.

★ WHERE OUTPUT MAY GO. An `--out` inside the repository must be at a path `git check-ignore` already ignores; an
`--out` outside the repository is unrestricted. A 244 MB corpus written to a tracked path is an untracked-file
avalanche in `git status` and, worse, a source-tree change under any instrument that fingerprints the tree.

⛔ ANCHOR AGREEMENT IS REPORTED, NOT ASSUMED, AND NOT THE DEFAULT VERDICT. A before/after arm is *supposed* to move
rows; a gate re-run is not. The anchor comparison always prints; `--require-anchors` turns disagreement into a
refusal. The run itself fails only on a RUN failure.

Usage
-----
    python3 tools/run_corpus.py --out DIR [--jobs N] [--lus PATH] [--engine meshroute] [--require-anchors]
    python3 tools/run_corpus.py --compare DIR_A DIR_B      # the determinism gate: jobs=1 vs jobs=N
    python3 tools/run_corpus.py --validate DIR             # link ③ alone, on one run directory
    python3 tools/run_corpus.py --selftest                 # prove every refusal above can fire

Linux/POSIX-only: it relies on process groups and `os.replace` within one filesystem.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
BASELINE_PATH = ROOT / "simulation" / "BASELINE.md"
SIMULATION_DIR = ROOT / "simulation"
DEFAULT_LUS = ROOT.parent / "lora-universal-simulator" / "build" / "orchestrator" / "lus"
DEFAULT_ENGINE = "meshroute"
MANIFEST_SCHEMA = 2

SECTION_RE = re.compile(r"^### 36/36 corpus\b", re.MULTILINE)
EXPECTED_ROWS = 36
ROW_RE = re.compile(
    r"^(?P<name>\S+)\s+(?P<md5>[0-9a-f]{8})\s+lus:\s+(?P<events>\d+)\s+events emitted,\s+"
    r"(?P<failures>\d+)\s+assertion failure\(s\)\s*$"
)
# `lus` reports this on stderr. EXACTLY ONE must appear in a scenario's log — see `judge_result`.
LUS_RESULT_RE = re.compile(r"^lus:\s+(?P<events>\d+)\s+events emitted,\s+(?P<failures>\d+)\s+assertion failure\(s\)",
                           re.MULTILINE)

# ⚠ NOT a scenario: a bare `{node: {node: dB}}` link matrix with no `nodes`/`events` keys.
NON_SCENARIO_STEMS = frozenset({"topo_9node"})

TERM_GRACE_S = 5.0
POLL_S = 0.05


class CorpusError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise CorpusError(message)


def digest_file(path: Path) -> tuple[str, str, int]:
    """(md5, sha256, bytes) in one pass — the streams reach ~1 GB, so read them once."""
    md5, sha, size = hashlib.md5(), hashlib.sha256(), 0
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            md5.update(chunk)
            sha.update(chunk)
            size += len(chunk)
    return md5.hexdigest(), sha.hexdigest(), size


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


# ===== ① BINDING THE CORPUS TO THE ANCHOR TABLE ===================================================================
# Both functions take their input as an argument so `--selftest` can feed them a corrupted table / a drifted
# `simulation/` listing IN MEMORY and prove the refusal fires, without ever writing the repository.

def parse_anchor_table(text: str) -> list[dict[str, Any]]:
    """The `### 36/36 corpus` fenced block -> one row per scenario. Refuses on any structural surprise."""
    headers = list(SECTION_RE.finditer(text))
    require(len(headers) == 1,
            f"expected exactly ONE '### 36/36 corpus' section in BASELINE.md, found {len(headers)}")
    tail = text[headers[0].end():]
    fence = tail.find("\n```")
    require(fence >= 0, "no fenced anchor block follows the '### 36/36 corpus' header")
    line_end = tail.find("\n", fence + len("\n```"))
    require(line_end >= 0, "the anchor fence is not terminated")
    close = tail.find("\n```", line_end)
    require(close >= 0, "the anchor block's closing fence is missing")

    rows: list[dict[str, Any]] = []
    seen: set[str] = set()
    for lineno, line in enumerate(tail[line_end + 1:close].splitlines(), 1):
        if not line.strip():
            continue
        match = ROW_RE.match(line)
        require(match is not None, f"unparsable anchor row {lineno}: {line!r}")
        name = match.group("name")
        require(name not in seen, f"duplicate scenario row in the anchor table: {name}")
        seen.add(name)
        rows.append({
            "name": name,
            "anchor_md5_prefix": match.group("md5"),
            "anchor_events": int(match.group("events")),
            "anchor_assertion_failures": int(match.group("failures")),
        })
    require(len(rows) == EXPECTED_ROWS,
            f"anchor table has {len(rows)} rows, expected {EXPECTED_ROWS} — the corpus set has drifted")
    return rows


def bind_scenarios(rows: list[dict[str, Any]], stems: list[str]) -> None:
    """Require the anchored names and the on-disk `simulation/*.json` stems to agree EXACTLY."""
    anchored = {row["name"] for row in rows}
    present = set(stems)
    duplicates = sorted({stem for stem in stems if stems.count(stem) > 1})
    require(not duplicates, f"duplicate scenario stems on disk: {duplicates}")
    missing = sorted(anchored - present)
    require(not missing, f"anchored scenario JSON missing from simulation/: {missing}")
    unexpected = sorted(present - anchored - NON_SCENARIO_STEMS)
    require(not unexpected,
            f"simulation/ holds JSON the anchor table does not name: {unexpected} — anchor it or list it as a "
            f"non-scenario; a silently skipped file is a corpus that shrank without saying so")


def corpus_rows() -> list[dict[str, Any]]:
    require(BASELINE_PATH.is_file(), f"missing {BASELINE_PATH}")
    rows = parse_anchor_table(BASELINE_PATH.read_text(encoding="utf-8"))
    bind_scenarios(rows, [path.stem for path in sorted(SIMULATION_DIR.glob("*.json"))])
    return rows


# ===== ① FREEZING THE INPUTS ======================================================================================

def fingerprint_inputs(lus: Path, rows: list[dict[str, Any]], scenario_dir: Path,
                       baseline: Path) -> dict[str, Any]:
    """The run's input authority: one hash per file that can change what the streams contain."""
    return {
        "lus_sha256": sha256_file(lus),
        "baseline_sha256": sha256_file(baseline),
        "scenarios": {row["name"]: sha256_file(scenario_dir / f"{row['name']}.json") for row in rows},
    }


def require_inputs_stable(before: dict[str, Any], after: dict[str, Any]) -> None:
    """⛔ Injectable so `--selftest` can prove the refusal fires without touching the repository."""
    if before == after:
        return
    moved = []
    if before["lus_sha256"] != after["lus_sha256"]:
        moved.append("lus")
    if before["baseline_sha256"] != after["baseline_sha256"]:
        moved.append("simulation/BASELINE.md")
    moved += sorted(name for name, digest in before["scenarios"].items()
                    if after["scenarios"].get(name) != digest)
    moved += sorted(f"{name} (appeared)" for name in after["scenarios"] if name not in before["scenarios"])
    raise CorpusError(f"run inputs changed while the corpus was being produced: {moved} — the streams are not "
                      f"attributable to one tree state")


SNAPSHOT_DIRNAME = "inputs"
SNAPSHOT_SCENARIO_DIRNAME = "simulation"


def snapshot_paths(directory: Path, manifest: dict[str, Any]) -> tuple[Path, Path, Path]:
    """(lus, BASELINE, scenario dir) inside a run's retained snapshot, named by the manifest itself."""
    inputs = directory / manifest["inputs"]["snapshot_dir"]
    return (inputs / manifest["inputs"]["lus_filename"],
            inputs / manifest["inputs"]["baseline_filename"],
            inputs / manifest["inputs"]["scenario_dirname"])


def snapshot_inputs(output: Path, lus: Path, rows: list[dict[str, Any]],
                    authority: dict[str, Any]) -> tuple[Path, Path, Path]:
    """Copy the inputs into the run dir and RUN FROM THE COPIES. Returns (lus, BASELINE, scenario dir)."""
    inputs = output / SNAPSHOT_DIRNAME
    scenarios = inputs / SNAPSHOT_SCENARIO_DIRNAME
    scenarios.mkdir(parents=True, exist_ok=False)
    frozen_lus = inputs / lus.name
    shutil.copy2(lus, frozen_lus)
    frozen_lus.chmod(0o755)
    frozen_baseline = inputs / BASELINE_PATH.name
    shutil.copy2(BASELINE_PATH, frozen_baseline)
    for row in rows:
        shutil.copy2(SIMULATION_DIR / f"{row['name']}.json", scenarios / f"{row['name']}.json")
    # A copy that does not reproduce the authority means the source moved DURING the copy.
    require_inputs_stable(authority, fingerprint_inputs(frozen_lus, rows, scenarios, frozen_baseline))
    return frozen_lus, frozen_baseline, scenarios


# ===== THE RUN ====================================================================================================

def path_within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def git_ignores(path: Path) -> bool:
    try:
        return subprocess.run(["git", "check-ignore", "-q", str(path)], cwd=ROOT).returncode == 0
    except OSError:
        return False


def validate_output_dir(value: str) -> Path:
    output = Path(value)
    if not output.is_absolute():
        output = Path.cwd() / output
    output = output.resolve()
    require(output != ROOT, "output directory cannot be the repository root")
    # ★ THE RULE: inside the repo, only where git already ignores. A 244 MB corpus under a tracked path is an
    #   untracked-file avalanche AND a source-tree change to every instrument that fingerprints the tree.
    if path_within(output, ROOT):
        require(git_ignores(output),
                f"an --out inside the repository must be at a git-ignored path; {output.relative_to(ROOT)} is not "
                f"ignored (use a path outside the repo, or one .gitignore already covers)")
    if output.exists():
        require(output.is_dir(), f"output path exists and is not a directory: {output}")
        require(not any(output.iterdir()), f"output directory is not empty: {output} (runs use FRESH directories)")
    return output


class Job:
    def __init__(self, row: dict[str, Any], scenario: Path, source_scenario: Path,
                 partial: Path, final: Path, rejected: Path, log: Path) -> None:
        self.row = row
        self.name = row["name"]
        self.scenario = scenario                 # the SNAPSHOT copy that is executed
        self.source_scenario = source_scenario   # the original, for the manifest's provenance
        self.partial = partial
        self.final = final
        self.rejected = rejected
        self.log = log
        self.process: subprocess.Popen | None = None
        self.handle = None
        self.started = 0.0
        self.wall_ms = 0
        self.returncode: int | None = None

    def start(self, command: list[str]) -> None:
        self.handle = self.log.open("wb")
        self.handle.write(f"$ {' '.join(command)}\n".encode())
        self.handle.flush()
        self.started = time.monotonic()
        self.process = subprocess.Popen(
            command, cwd=ROOT, stdout=self.handle, stderr=subprocess.STDOUT, start_new_session=True
        )

    def finish(self) -> None:
        self.wall_ms = int((time.monotonic() - self.started) * 1000)
        if self.handle is not None:
            self.handle.close()
            self.handle = None


def kill_all(jobs: list[Job]) -> None:
    """SIGTERM the whole process GROUP of every live child, then SIGKILL the stragglers, then REAP each one."""
    live = [job for job in jobs if job.process is not None and job.process.poll() is None]
    for job in live:
        try:
            os.killpg(os.getpgid(job.process.pid), signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass
    deadline = time.monotonic() + TERM_GRACE_S
    while time.monotonic() < deadline and any(job.process.poll() is None for job in live):
        time.sleep(POLL_S)
    for job in live:
        if job.process.poll() is None:
            try:
                os.killpg(os.getpgid(job.process.pid), signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
    for job in live:
        try:
            job.process.wait(timeout=TERM_GRACE_S)   # the reap; without it the children become zombies
        except subprocess.TimeoutExpired:
            pass
        job.finish()


def judge_result(returncode: int | None, log_text: str, produced: bool) -> tuple[str | None, int | None, int | None]:
    """② THE PROMOTION PREDICATE. Returns (failure or None, events, assertion_failures).

    ⛔ Every arm here runs BEFORE `os.replace()`. Round 1 promoted first and judged second, so an exit-0 `lus`
    that wrote a stream but printed no summary left a promoted file inside a run reported as failed.
    """
    results = LUS_RESULT_RE.findall(log_text)
    events = int(results[0][0]) if results else None
    failures = int(results[0][1]) if results else None
    if returncode != 0:
        return f"lus exited {returncode}", events, failures
    if not produced:
        return "lus exited 0 but produced no output file", events, failures
    if len(results) == 0:
        return "lus exited 0 but printed no result line — nothing states what it produced", None, None
    if len(results) > 1:
        distinct = sorted(set(results))
        return (f"lus printed {len(results)} result lines ({distinct}) — the run cannot be summarised by one",
                events, failures)
    if failures != 0:
        return f"lus reported {failures} assertion failure(s)", events, failures
    return None, events, failures


def collect_result(job: Job, authority_sha: str) -> dict[str, Any]:
    """Judge ONE finished scenario. Promotion happens here and nowhere else, and only after `judge_result`."""
    record: dict[str, Any] = {
        "name": job.name,
        "scenario_path": str(job.source_scenario.relative_to(ROOT)),
        "scenario_sha256": authority_sha,
        "exit_status": job.returncode,
        "promoted": False,
    }
    log_text = job.log.read_text(encoding="utf-8", errors="replace")
    produced = job.partial.exists()
    failure, events, failures = judge_result(job.returncode, log_text, produced)
    record["events"] = events
    record["assertion_failures"] = failures

    if failure is not None:
        # ⛔ NOT PROMOTED. Kept under rejected/ for diagnosis — `streams/` only ever holds validated streams.
        if produced:
            record["rejected_bytes"] = job.partial.stat().st_size
            os.replace(job.partial, job.rejected)
            record["rejected_path"] = str(job.rejected.relative_to(job.rejected.parents[1]))
        record["failure"] = failure
        return record

    md5, sha, size = digest_file(job.partial)
    os.replace(job.partial, job.final)          # ← the ONLY way a stream appears, and only past every check above
    record.update({
        "promoted": True,
        "output_bytes": size,
        "output_md5": md5,
        "output_sha256": sha,
        "anchor_md5_prefix": job.row["anchor_md5_prefix"],
        "anchor_events": job.row["anchor_events"],
        "anchor_assertion_failures": job.row["anchor_assertion_failures"],
    })
    record["anchor_match"] = bool(
        md5.startswith(job.row["anchor_md5_prefix"])
        and events == job.row["anchor_events"]
        and failures == job.row["anchor_assertion_failures"])
    return record


def usable_cores() -> int:
    try:
        return len(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        return os.cpu_count() or 1


def git_identity() -> dict[str, str]:
    try:
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        status = subprocess.check_output(
            ["git", "status", "--porcelain=v1", "--untracked-files=all"], cwd=ROOT)
    except (OSError, subprocess.CalledProcessError):
        return {"git_head": "unavailable", "git_status_sha256": "unavailable"}
    return {"git_head": head, "git_status_sha256": hashlib.sha256(status).hexdigest()}


def run_corpus(output: Path, jobs: int, lus: Path, engine: str, require_anchors: bool) -> int:
    require(jobs >= 1, f"--jobs must be >= 1, got {jobs}")
    require(lus.is_file(), f"lus binary not found: {lus}")
    require(os.access(lus, os.X_OK), f"lus is not executable: {lus}")
    rows = corpus_rows()

    # ① Freeze first: hash the originals, copy, verify the copy, then execute the copy and nothing else.
    authority = fingerprint_inputs(lus, rows, SIMULATION_DIR, BASELINE_PATH)
    streams, logs, partials, rejected = (output / "streams", output / "logs",
                                         output / ".partial", output / "rejected")
    for directory in (streams, logs, partials, rejected):
        directory.mkdir(parents=True, exist_ok=False)
    frozen_lus, frozen_baseline, frozen_scenarios = snapshot_inputs(output, lus, rows, authority)

    # ⓘ Longest-first is a pure MAKESPAN choice and cannot affect content: each scenario is an independent process
    #   writing its own file. The order comes from the anchored event counts, so it is itself deterministic.
    ordered = sorted(rows, key=lambda row: (-row["anchor_events"], row["name"]))
    queue = [
        Job(row, frozen_scenarios / f"{row['name']}.json", SIMULATION_DIR / f"{row['name']}.json",
            partials / f"{row['name']}.ndjson", streams / f"{row['name']}.ndjson",
            rejected / f"{row['name']}.ndjson", logs / f"{row['name']}.log")
        for row in ordered
    ]

    interrupted: list[str] = []

    def on_signal(signum, _frame):
        interrupted.append(signal.Signals(signum).name)

    previous = {sig: signal.signal(sig, on_signal) for sig in (signal.SIGINT, signal.SIGTERM)}
    records: list[dict[str, Any]] = []
    pending, running = list(queue), []
    started_wall = time.monotonic()
    try:
        while pending or running:
            if interrupted:
                raise CorpusError(f"interrupted by {interrupted[0]} — killing and reaping {len(running)} child "
                                  f"process(es); no partial stream is promoted")
            while pending and len(running) < jobs:
                job = pending.pop(0)
                job.start([str(frozen_lus), "-e", engine, str(job.scenario), str(job.partial)])
                running.append(job)
            done = [job for job in running if job.process.poll() is not None]
            if not done:
                time.sleep(POLL_S)
                continue
            for job in done:
                running.remove(job)
                job.returncode = job.process.returncode
                job.finish()
                record = collect_result(job, authority["scenarios"][job.name])
                records.append(record)
                mark = "ok " if record.get("promoted") else "FAIL"
                print(f"  [{len(records):2d}/{len(queue)}] {mark} {job.name:<44} "
                      f"{record.get('output_md5', '-')[:8]} events={record.get('events')} "
                      f"({job.wall_ms/1000:.1f}s)")
                sys.stdout.flush()
    finally:
        kill_all(pending + running)
        for sig, handler in previous.items():
            signal.signal(sig, handler)
        for job in queue:                       # no partial ever survives a run, successful or not
            if job.partial.exists():
                job.partial.unlink()

    wall = time.monotonic() - started_wall
    records.sort(key=lambda record: record["name"])
    failures = [record for record in records if "failure" in record or not record.get("promoted")]

    # ① The other half of the freeze, BOTH SIDES. QG round 2: round 1 rehashed only the ORIGINALS, so a snapshot
    #   corrupted during the run was invisible — and the snapshot is what actually produced the streams.
    inputs_stable_error = None
    for label, refreshed in (
        ("originals", lambda: fingerprint_inputs(lus, rows, SIMULATION_DIR, BASELINE_PATH)),
        ("the retained inputs/ snapshot", lambda: fingerprint_inputs(frozen_lus, rows, frozen_scenarios,
                                                                     frozen_baseline)),
    ):
        try:
            require_inputs_stable(authority, refreshed())
        except CorpusError as exc:
            inputs_stable_error = f"{label}: {exc}"
            break

    manifest = {
        "schema": MANIFEST_SCHEMA,
        "generated_by": "tools/run_corpus.py",
        "engine": engine,
        "jobs": jobs,
        "lus_path": str(lus),
        "lus_sha256": authority["lus_sha256"],          # ← the PRE-run authority, not a post-hoc hash
        "baseline_path": str(BASELINE_PATH.relative_to(ROOT)),
        "baseline_sha256": authority["baseline_sha256"],
        # ★ The snapshot's own inventory, as FIRST-CLASS manifest data: `validate_run` re-hashes every one of
        #   these files on disk and binds each scenario record's `scenario_sha256` to the entry here.
        "inputs": {
            "snapshot_dir": SNAPSHOT_DIRNAME,
            "lus_filename": frozen_lus.name,
            "baseline_filename": frozen_baseline.name,
            "scenario_dirname": SNAPSHOT_SCENARIO_DIRNAME,
            "lus_sha256": authority["lus_sha256"],
            "baseline_sha256": authority["baseline_sha256"],
            "scenarios": dict(sorted(authority["scenarios"].items())),
        },
        "inputs_stable": inputs_stable_error is None,
        "scenario_count": len(rows),
        "scenarios": records,
        "timing_ms": {job.name: job.wall_ms for job in queue if job.wall_ms},   # ⚠ NOT a comparison field
        "wall_clock_s": round(wall, 3),
        "host": {"platform": sys.platform, "cpu_count": os.cpu_count(), "usable_cores": usable_cores()},
        "source": git_identity(),
    }
    temporary = output / "manifest.json.partial"
    temporary.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    os.replace(temporary, output / "manifest.json")
    partials.rmdir()
    if not any(rejected.iterdir()):
        rejected.rmdir()

    print()
    print(f"  wall_clock={wall:.1f}s jobs={jobs} usable_cores={usable_cores()}")
    print(f"  manifest={output / 'manifest.json'}")
    if inputs_stable_error:
        print(f"REFUSED: {inputs_stable_error}")
        return 1
    if failures:
        print(f"REFUSED: {len(failures)} scenario failure(s)")
        for record in failures:
            print(f"    {record['name']}: {record.get('failure', 'not promoted')} (exit {record['exit_status']})")
        return 1

    # ③ The run judges itself with the SAME function `--compare` will use. A run that cannot pass validation is
    #   not reported as a pass, however green the loop above looked.
    validate_run(output)
    matched = sum(1 for record in records if record.get("anchor_match"))
    drift = [record["name"] for record in records if not record["anchor_match"]]
    print(f"PASS: {len(records)}/{len(rows)} streams produced and validated, 0 failures")
    print(f"  anchors: {matched}/{len(records)} rows reproduce simulation/BASELINE.md"
          + (f"; DIFFER: {', '.join(drift)}" if drift else ""))
    if drift and require_anchors:
        print("REFUSED: --require-anchors and the anchor table disagree")
        return 1
    return 0


# ===== ③ VALIDATING A RUN DIRECTORY ===============================================================================

def load_manifest(path: Path) -> tuple[Path, dict[str, Any]]:
    directory = path if path.is_dir() else path.parent
    manifest_path = directory / "manifest.json"
    require(manifest_path.is_file(), f"no manifest.json in {directory}")
    try:
        value = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CorpusError(f"cannot read manifest {manifest_path}: {exc}") from exc
    require(value.get("schema") == MANIFEST_SCHEMA,
            f"unsupported manifest schema {value.get('schema')} in {manifest_path} "
            f"(this runner writes {MANIFEST_SCHEMA})")
    return directory, value


def snapshot_problems(directory: Path, manifest: dict[str, Any], record_names: list[str]) -> list[str]:
    """★ QG round 2, GAP 1: the RETAINED snapshot is what produced the streams, so it must be re-verified — the
    copied `lus`, the copied `BASELINE.md`, and the EXACT scenario set (names, hashes, count). Round 1 kept the
    snapshot and never looked at it again, so a scenario copy edited after the run still validated."""
    problems: list[str] = []
    inputs = manifest.get("inputs")
    if not isinstance(inputs, dict):
        return ["the manifest carries no inputs/ snapshot inventory (pre-schema-2 run?)"]
    frozen_lus, frozen_baseline, frozen_scenarios = snapshot_paths(directory, manifest)

    for label, path, expected, top_level in (
        ("lus", frozen_lus, inputs.get("lus_sha256"), manifest.get("lus_sha256")),
        ("BASELINE.md", frozen_baseline, inputs.get("baseline_sha256"), manifest.get("baseline_sha256")),
    ):
        if expected != top_level:
            problems.append(f"snapshot {label}: the inputs inventory and the manifest's own hash disagree")
        if not path.is_file():
            problems.append(f"snapshot {label} is MISSING from {path.parent.name}/")
            continue
        actual = sha256_file(path)
        if actual != expected:
            problems.append(f"snapshot {label} does NOT match the manifest ({actual[:12]} vs "
                            f"{str(expected)[:12]}) — the frozen input was altered after the run")

    declared = inputs.get("scenarios")
    if not isinstance(declared, dict):
        return problems + ["the inputs inventory carries no scenario hashes"]
    if len(declared) != EXPECTED_ROWS:
        problems.append(f"the inputs inventory names {len(declared)} scenarios, expected {EXPECTED_ROWS}")
    if set(declared) != set(record_names):
        problems.append(f"the inputs inventory and the scenario records name different sets: "
                        f"only-inputs={sorted(set(declared) - set(record_names))} "
                        f"only-records={sorted(set(record_names) - set(declared))}")

    if not frozen_scenarios.is_dir():
        return problems + [f"snapshot scenario directory is MISSING: {frozen_scenarios}"]
    for name, expected in sorted(declared.items()):
        path = frozen_scenarios / f"{name}.json"
        if not path.is_file():
            problems.append(f"snapshot scenario {name}.json is MISSING")
            continue
        actual = sha256_file(path)
        if actual != expected:
            problems.append(f"snapshot scenario {name}.json does NOT match the manifest ({actual[:12]} vs "
                            f"{str(expected)[:12]}) — the frozen input was altered after the run")
    unexpected = sorted(path.name for path in frozen_scenarios.iterdir() if path.stem not in declared)
    if unexpected:
        problems.append(f"the snapshot holds scenario files the manifest does not name: {unexpected}")

    # ★ THE BINDING. Without this a record could name a scenario hash the snapshot never contained.
    for record in manifest.get("scenarios", []):
        expected = declared.get(record.get("name"))
        if expected is not None and record.get("scenario_sha256") != expected:
            problems.append(f"{record.get('name')}: the record's scenario_sha256 is not the snapshot's")
    return problems


def validate_run(directory: Path) -> dict[str, Any]:
    """⛔ THE GATEKEEPER. A manifest is evidence only if it describes a COMPLETE, SUCCESSFUL run whose streams are
    STILL the ones it hashed. Called at the end of a run and again by `--compare`, so both trust the same judge."""
    directory, manifest = load_manifest(directory)
    problems: list[str] = []
    records = manifest.get("scenarios", [])
    names = [record.get("name") for record in records]

    if not manifest.get("inputs_stable", False):
        problems.append("the run's inputs changed while it ran (inputs_stable=false)")
    if len(names) != EXPECTED_ROWS:
        problems.append(f"{len(names)} scenario records, expected {EXPECTED_ROWS}")
    if manifest.get("scenario_count") != EXPECTED_ROWS:
        problems.append(f"manifest scenario_count={manifest.get('scenario_count')}, expected {EXPECTED_ROWS}")
    if manifest.get("scenario_count") != len(names):
        problems.append(f"manifest scenario_count={manifest.get('scenario_count')} but it carries "
                        f"{len(names)} records")
    duplicates = sorted({name for name in names if names.count(name) > 1})
    if duplicates:
        problems.append(f"duplicate scenario records: {duplicates}")
    problems += snapshot_problems(directory, manifest, names)

    streams = directory / "streams"
    if not streams.is_dir():
        problems.append("no streams/ directory")
        raise CorpusError(f"{directory} is not a usable corpus run:\n    " + "\n    ".join(problems))

    for record in records:
        name = record.get("name")
        if record.get("exit_status") != 0:
            problems.append(f"{name}: exit_status={record.get('exit_status')}")
        if not record.get("promoted"):
            problems.append(f"{name}: not promoted")
        if "failure" in record:
            problems.append(f"{name}: recorded failure {record['failure']!r}")
        if record.get("assertion_failures") != 0:
            problems.append(f"{name}: assertion_failures={record.get('assertion_failures')}")
        if not isinstance(record.get("events"), int):
            problems.append(f"{name}: no event count")
        path = streams / f"{name}.ndjson"
        if not path.is_file():
            problems.append(f"{name}: stream file is MISSING from streams/")
            continue
        md5, sha, size = digest_file(path)
        if sha != record.get("output_sha256") or md5 != record.get("output_md5") \
                or size != record.get("output_bytes"):
            problems.append(f"{name}: stream on disk does NOT match the manifest "
                            f"(md5 {md5[:8]} vs {str(record.get('output_md5'))[:8]}, {size} vs "
                            f"{record.get('output_bytes')} bytes)")

    unexpected = sorted(path.name for path in streams.iterdir() if path.stem not in set(names))
    if unexpected:
        problems.append(f"streams/ holds files no record names: {unexpected}")

    if problems:
        raise CorpusError(f"{directory} is not a usable corpus run:\n    " + "\n    ".join(problems))
    return manifest


# ===== ④ THE DETERMINISM GATE =====================================================================================
# ⚠ Exactly these fields, and NOT `timing_ms`/`wall_clock_s`/`jobs`/`lus_path`: a comparison including the
#   wall-clock could never pass, and one including `jobs` could never be run.
COMPARISON_TOP = ("schema", "engine", "scenario_count", "lus_sha256", "baseline_sha256")
COMPARISON_SCENARIO = ("scenario_path", "scenario_sha256", "exit_status", "promoted", "events",
                       "assertion_failures", "output_bytes", "output_md5", "output_sha256")


def compare_runs(first_path: Path, second_path: Path) -> int:
    # ⛔ VALIDATE BEFORE COMPARING. Round 1 compared raw JSON, so two identically FAILED runs printed PASS.
    first = validate_run(first_path)
    second = validate_run(second_path)
    mismatches = [field for field in COMPARISON_TOP if first.get(field) != second.get(field)]
    left = {record["name"]: record for record in first["scenarios"]}
    right = {record["name"]: record for record in second["scenarios"]}
    if set(left) != set(right):
        mismatches.append(f"scenario sets differ: only-A={sorted(set(left)-set(right))} "
                          f"only-B={sorted(set(right)-set(left))}")
    for name in sorted(set(left) & set(right)):
        for field in COMPARISON_SCENARIO:
            if left[name].get(field) != right[name].get(field):
                mismatches.append(f"{name}.{field}: {left[name].get(field)!r} != {right[name].get(field)!r}")
    if mismatches:
        print(f"REFUSED: {len(mismatches)} difference(s) between the two runs")
        for line in mismatches[:40]:
            print(f"    {line}")
        return 1
    print(f"PASS: byte-identical corpora — both runs VALIDATED, {len(left)}/{len(left)} streams agree on md5, "
          f"sha256, size, event count and assertion count")
    print(f"  A: jobs={first['jobs']} wall={first['wall_clock_s']}s    B: jobs={second['jobs']} "
          f"wall={second['wall_clock_s']}s")
    return 0


# ===== SELFTEST — every refusal above, proven able to FIRE =========================================================
# ⚠ §18.0.3 discipline: a green corpus run is evidence only if RED was reachable. The controls drive the REAL
#   runner (subprocess, real scheduler, real signals) against a STUB `lus`, in a temporary directory. Nothing under
#   the repository is written, and the anchor table is only ever read.

STUB = r'''#!/usr/bin/env python3
import os, sys, time
mode = os.environ.get("STUB_MODE", "ok")
target = os.environ.get("STUB_TARGET", "")
out = sys.argv[-1]
name = os.path.basename(out)[:-len(".ndjson")]
hit = (name == target)
if mode == "sleep":
    open(os.path.join(os.environ["STUB_PIDDIR"], str(os.getpid())), "w").write(name)
    time.sleep(600)
    sys.exit(0)
if mode == "fail" and hit:
    sys.stderr.write("stub: exploding\n")
    sys.exit(3)
if mode == "partial" and hit:
    open(out, "w").write("{\"partial\": true}\n")
    sys.stderr.write("stub: wrote a plausible partial then died\n")
    sys.exit(4)
if mode == "nooutput" and hit:
    sys.stderr.write("lus: 7 events emitted, 0 assertion failure(s)\n")
    sys.exit(0)
body = "".join("{\"n\":\"%s\",\"i\":%d}\n" % (name, i) for i in range(5))
if mode == "nondet":
    body += "{\"pid\":%d}\n" % os.getpid()
open(out, "w").write(body)
if mode == "noresult" and hit:
    sys.stderr.write("stub: finished, but says nothing about what it produced\n")
    sys.exit(0)
if mode == "assertfail" and hit:
    sys.stderr.write("lus: 5 events emitted, 3 assertion failure(s)\n")
    sys.exit(0)
if mode == "dupresult" and hit:
    sys.stderr.write("lus: 5 events emitted, 0 assertion failure(s)\n")
    sys.stderr.write("lus: 9 events emitted, 0 assertion failure(s)\n")
    sys.exit(0)
sys.stderr.write("lus: 5 events emitted, 0 assertion failure(s)\n")
sys.exit(0)
'''


def _stub_run(tmp: Path, label: str, mode: str, target: str, jobs: int, piddir: Path | None = None):
    stub = tmp / "stub_lus.py"
    if not stub.exists():
        stub.write_text(STUB, encoding="utf-8")
        stub.chmod(0o755)
    out = tmp / label
    environment = dict(os.environ, STUB_MODE=mode, STUB_TARGET=target,
                       STUB_PIDDIR=str(piddir) if piddir else str(tmp))
    return subprocess.Popen(
        [sys.executable, str(Path(__file__).resolve()), "--out", str(out), "--jobs", str(jobs),
         "--lus", str(stub)],
        env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True), out


def _stub_complete(tmp: Path, label: str, mode: str, target: str, jobs: int):
    process, out = _stub_run(tmp, label, mode, target, jobs)
    stdout, _ = process.communicate(timeout=300)
    return process.returncode, stdout, out


def selftest() -> int:
    import copy as copy_module
    import tempfile
    results: list[tuple[bool, str, str]] = []

    # `kind="neg"`: a guarded property is BROKEN and the instrument must refuse -> RED is the required outcome.
    # `kind="pos"`: nothing is broken and it must NOT refuse -> a green that makes the REDs mean something.
    def control(ok: bool, label: str, detail: str = "", kind: str = "neg") -> None:
        results.append((ok, label, detail))
        if kind == "neg":
            verdict = "RED   (control fired) " if ok else "⛔ GREEN (control DEAD)"
        else:
            verdict = "GREEN (as required)   " if ok else "⛔ RED   (false alarm) "
        print(f"  {verdict} {label}" + (f"\n           {detail}" if detail else ""))

    def refuses(callable_):
        try:
            callable_()
        except CorpusError as exc:
            return True, str(exc).replace("\n", " ")[:200]
        return False, "accepted without complaint"

    baseline = BASELINE_PATH.read_text(encoding="utf-8")
    good = parse_anchor_table(baseline)
    print(f"\nanchor table parses clean: {len(good)} rows\n")

    # --- ① structural controls, in memory ------------------------------------------------------------------------
    header = SECTION_RE.search(baseline)
    fence_start = baseline.index("\n```", header.end())
    first_row_start = baseline.index("\n", fence_start + 4) + 1
    first_row_end = baseline.index("\n", first_row_start) + 1
    first_row = baseline[first_row_start:first_row_end]

    ok, detail = refuses(lambda: parse_anchor_table(baseline[:first_row_start] + baseline[first_row_end:]))
    control(ok, "a DROPPED anchor row (35) is refused", detail)
    ok, detail = refuses(lambda: parse_anchor_table(baseline[:first_row_start] + first_row
                                                    + baseline[first_row_start:]))
    control(ok, "a DUPLICATE anchor row is refused", detail)
    ok, detail = refuses(lambda: parse_anchor_table(baseline[:first_row_start] + "s99_nonsense  zz lus: x\n"
                                                    + baseline[first_row_start:]))
    control(ok, "an UNPARSABLE anchor row is refused", detail)

    stems = [row["name"] for row in good] + sorted(NON_SCENARIO_STEMS)
    ok, detail = refuses(lambda: bind_scenarios(good, stems + ["s99_unanchored_newcomer"]))
    control(ok, "an UNEXPECTED scenario JSON in simulation/ is refused", detail)
    ok, detail = refuses(lambda: bind_scenarios(good, [s for s in stems if s != good[0]["name"]]))
    control(ok, "a MISSING scenario JSON is refused", detail)
    ok, detail = refuses(lambda: bind_scenarios(good, stems))
    control(not ok, "the LIVE simulation/ listing binds cleanly (the negative controls above mean something)",
            "" if not ok else detail, kind="pos")

    # --- ① the input freeze --------------------------------------------------------------------------------------
    authority = {"lus_sha256": "a", "baseline_sha256": "b", "scenarios": {row["name"]: "s" for row in good}}
    for label, mutate in (
        ("the lus BINARY changing mid-run is refused", lambda f: f.update(lus_sha256="CHANGED")),
        ("BASELINE.md changing mid-run is refused", lambda f: f.update(baseline_sha256="CHANGED")),
        ("a SCENARIO JSON changing mid-run is refused",
         lambda f: f["scenarios"].update({good[0]["name"]: "CHANGED"})),
    ):
        after = copy_module.deepcopy(authority)
        mutate(after)
        ok, detail = refuses(lambda a=after: require_inputs_stable(authority, a))
        control(ok, label, detail)
    ok, detail = refuses(lambda: require_inputs_stable(authority, copy_module.deepcopy(authority)))
    control(not ok, "unchanged inputs are accepted", "" if not ok else detail, kind="pos")

    # --- ① where output may go -----------------------------------------------------------------------------------
    ok, detail = refuses(lambda: validate_output_dir(str(ROOT / "corpus_out")))
    control(ok, "an --out at a TRACKED path inside the repository is refused", detail)
    ok, detail = refuses(lambda: validate_output_dir(str(ROOT / ".pio-measure" / "corpus_out")))
    control(not ok, "an --out at a git-IGNORED path inside the repository is accepted",
            "" if not ok else detail, kind="pos")

    # --- ②③④ runtime controls, real subprocesses -----------------------------------------------------------------
    with tempfile.TemporaryDirectory(prefix="run_corpus_selftest_") as raw:
        tmp = Path(raw)
        victim = good[0]["name"]

        code, log, clean_out = _stub_complete(tmp, "ok1", "ok", "", 4)
        control(code == 0 and len(list((clean_out / "streams").glob("*.ndjson"))) == EXPECTED_ROWS,
                f"a clean stub run PASSES and promotes all {EXPECTED_ROWS} streams (exit {code})",
                "" if code == 0 else log[-400:], kind="pos")

        # ② the promotion predicate — every arm must refuse AND leave streams/ without the victim
        for mode, label in (
            ("fail", f"a FAILED worker ({victim} exits 3) refuses and promotes nothing"),
            ("partial", "a PARTIAL output (bytes written, then a nonzero exit) refuses and is never promoted"),
            ("nooutput", "a MISSING result (exit 0, no file) refuses rather than passing 35/36"),
            ("noresult", "exit 0 + output + NO result line refuses and is NEVER PROMOTED"),
            ("assertfail", "exit 0 + output + NONZERO assertion failures refuses and is never promoted"),
            ("dupresult", "exit 0 + output + TWO conflicting result lines refuses and is never promoted"),
        ):
            code, log, out = _stub_complete(tmp, f"pred_{mode}", mode, victim, 4)
            promoted = (out / "streams" / f"{victim}.ndjson").exists()
            control(code != 0 and not promoted, label, f"exit={code} promoted={promoted}")

        # ⛔ THE REAL SIGINT TEST. Children are in their own process groups, so only the parent can kill them.
        piddir = tmp / "pids"
        piddir.mkdir()
        process, out = _stub_run(tmp, "sigint", "sleep", "", 4, piddir)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline and len(list(piddir.iterdir())) < 4:
            time.sleep(0.05)
        spawned = sorted(int(path.name) for path in piddir.iterdir())
        process.send_signal(signal.SIGINT)
        process.communicate(timeout=60)
        time.sleep(0.5)
        survivors = []
        for pid in spawned:
            try:                        # ⚠ the read IS the existence check — a dead pid raises, it does not lie
                cmdline = Path(f"/proc/{pid}/cmdline").read_bytes()
            except OSError:
                continue
            if b"stub_lus.py" in cmdline:
                survivors.append(pid)
        control(len(spawned) >= 4 and process.returncode != 0 and not survivors
                and not list((out / "streams").glob("*.ndjson")),
                "an INTERRUPTED PARENT (real SIGINT) kills and reaps every child — no orphan lus, nothing promoted",
                f"spawned={len(spawned)} exit={process.returncode} survivors={survivors}")

        # ③④ the manifest gatekeeper — the false-green shapes
        _, _, failed_a = _stub_complete(tmp, "failed_a", "fail", victim, 4)
        _, _, failed_b = _stub_complete(tmp, "failed_b", "fail", victim, 4)
        ok, detail = refuses(lambda: compare_runs(failed_a, failed_b))
        control(ok, "two IDENTICALLY FAILED runs are REFUSED, never compared equal (the worst false green)", detail)

        code_a, _, out_a = _stub_complete(tmp, "det1", "ok", "", 1)
        code_b, _, out_b = _stub_complete(tmp, "det4", "ok", "", 4)
        control(compare_runs(out_a, out_b) == 0 and code_a == 0 and code_b == 0,
                "jobs=1 and jobs=4 of the SAME stub compare EQUAL (the gate can pass)", kind="pos")

        deleted = tmp / "deleted"
        shutil.copytree(clean_out, deleted)
        (deleted / "streams" / f"{victim}.ndjson").unlink()
        ok, detail = refuses(lambda: compare_runs(out_a, deleted))
        control(ok, "a stream DELETED after the manifest was written is refused", detail)

        modified = tmp / "modified"
        shutil.copytree(clean_out, modified)
        with (modified / "streams" / f"{victim}.ndjson").open("a") as handle:
            handle.write('{"tampered":true}\n')
        ok, detail = refuses(lambda: compare_runs(out_a, modified))
        control(ok, "a stream MODIFIED after the manifest was written is refused", detail)

        extra = tmp / "extra"
        shutil.copytree(clean_out, extra)
        (extra / "streams" / "s99_intruder.ndjson").write_text("{}\n", encoding="utf-8")
        ok, detail = refuses(lambda: compare_runs(out_a, extra))
        control(ok, "an UNEXPECTED stream file in streams/ is refused", detail)

        # ★ QG round 2 GAP 1: the RETAINED SNAPSHOT. All four arms of the same class — a corrupted scenario copy
        #   was the reproduced defect; the lus copy, the BASELINE copy, a deleted file and an intruder are the
        #   symmetric arms, and they cost one line each.
        victim_scenario = f"{SNAPSHOT_SCENARIO_DIRNAME}/{victim}.json"
        for label, relative, action in (
            ("a copied SCENARIO corrupted after the run", victim_scenario, "append"),
            ("the copied LUS binary corrupted after the run", "stub_lus.py", "append"),
            ("the copied BASELINE.md corrupted after the run", BASELINE_PATH.name, "append"),
            ("a snapshot file DELETED after the run", victim_scenario, "delete"),
            ("an INTRUDER file in the snapshot's scenario dir",
             f"{SNAPSHOT_SCENARIO_DIRNAME}/s99_intruder.json", "create"),
        ):
            tampered = tmp / f"snap_{action}_{relative.replace('/', '_')}"
            shutil.copytree(clean_out, tampered)
            path = tampered / SNAPSHOT_DIRNAME / relative
            if action == "append":
                with path.open("a") as handle:
                    handle.write("\n# tampered\n")
            elif action == "delete":
                path.unlink()
            else:
                path.write_text("{}\n", encoding="utf-8")
            ok, detail = refuses(lambda d=tampered: validate_run(d))
            control(ok, label, detail)

        truncated = tmp / "truncated"
        shutil.copytree(clean_out, truncated)
        manifest = json.loads((truncated / "manifest.json").read_text())
        manifest["scenarios"] = manifest["scenarios"][:-1]
        (truncated / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        ok, detail = refuses(lambda: compare_runs(out_a, truncated))
        control(ok, f"a manifest with {EXPECTED_ROWS - 1} records is refused", detail)

        miscounted = tmp / "miscounted"
        shutil.copytree(clean_out, miscounted)
        manifest = json.loads((miscounted / "manifest.json").read_text())
        manifest["scenario_count"] = EXPECTED_ROWS - 1
        (miscounted / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        ok, detail = refuses(lambda: validate_run(miscounted))
        control(ok, "a manifest whose scenario_count disagrees with its records is refused", detail)

        rebound = tmp / "rebound"
        shutil.copytree(clean_out, rebound)
        manifest = json.loads((rebound / "manifest.json").read_text())
        for record in manifest["scenarios"]:
            if record["name"] == victim:
                record["scenario_sha256"] = "0" * 64
        (rebound / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        ok, detail = refuses(lambda: validate_run(rebound))
        control(ok, "a record whose scenario_sha256 is not the snapshot's is refused (the binding)", detail)

        code_c, _, out_c = _stub_complete(tmp, "nondet", "nondet", "", 4)
        control(code_c == 0 and compare_runs(out_a, out_c) != 0,
                "a deliberately NON-DETERMINISTIC stub is CAUGHT by --compare (the gate is not vacuous)")

    print()
    failed = [label for ok, label, _ in results if not ok]
    if failed:
        print(f"SELFTEST FAIL — {len(failed)}/{len(results)} controls did not behave:")
        for label in failed:
            print(f"    {label}")
        return 1
    print(f"SELFTEST PASS — {len(results)}/{len(results)} controls behaved. "
          f"A green corpus run from this file is a measurement.")
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    result.add_argument("--out", help="fresh/empty output directory for streams, logs, inputs and the manifest")
    result.add_argument("--jobs", type=int, default=1,
                        help="concurrent lus processes; 1 is the sequential arbiter (default: 1)")
    result.add_argument("--lus", type=Path, default=DEFAULT_LUS, help=f"lus binary (default: {DEFAULT_LUS})")
    result.add_argument("--engine", default=DEFAULT_ENGINE, help=f"lus engine (default: {DEFAULT_ENGINE})")
    result.add_argument("--require-anchors", action="store_true",
                        help="turn disagreement with simulation/BASELINE.md into a refusal")
    result.add_argument("--compare", nargs=2, metavar=("A", "B"),
                        help="validate and compare two run directories — the determinism gate")
    result.add_argument("--validate", metavar="DIR", help="validate one run directory and say why not, if not")
    result.add_argument("--selftest", action="store_true", help="prove every refusal above can fire")
    return result


def main() -> int:
    args = parser().parse_args()
    if args.selftest:
        return selftest()
    if args.compare:
        return compare_runs(Path(args.compare[0]).resolve(), Path(args.compare[1]).resolve())
    if args.validate:
        manifest = validate_run(Path(args.validate).resolve())
        print(f"PASS: {Path(args.validate)} is a complete, successful, on-disk-verified corpus run "
              f"({manifest['scenario_count']} scenarios, jobs={manifest['jobs']})")
        return 0
    require(args.out is not None, "one of --out, --compare, --validate or --selftest is required")
    output = validate_output_dir(args.out)
    output.mkdir(parents=True, exist_ok=True)
    return run_corpus(output, args.jobs, args.lus.resolve(), args.engine, args.require_anchors)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CorpusError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
