#!/usr/bin/env python3
# MeshRoute — tools/probe_inbox_verbs/transcript.py
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# ★★★ §RADMIN-0c — THE CALLER-SHAPE EXTRACTOR AND BYTE/SIDE-EFFECT COMPARATOR.
#
# It answers ONE question and answers it by RUNNING code:
#
#     ⇒ FOR EVERY LINE IN THE INVENTORY-DERIVED MATRIX, DO THE USB SHAPE AND THE BLE SHAPE OF A GIVEN
#       `src/fw_main.cpp` PRODUCE THE SAME BYTES ON THE SAME SINKS AND THE SAME DEVICE-STATE DELTA?
#
# Run it twice — once on the pre-0c `fw_main.cpp` (`git show <base>:src/fw_main.cpp`), once on the current one —
# and compare the two transcripts with `--compare`. Exit 0 (every validated record identical) IS the C1 proof;
# anything else is the refactor's cost, named row by row and record by record. (A raw `diff` of the two files also
# shows their provenance — the head and the adapters identity carry each source's hash — which `--compare` reports
# and never compares.) Because BOTH arms link the SAME `src/firmware_commands.cpp`, the comparison isolates exactly
# the thing 0c changes: the CALLER SHAPE.
#
# ⛔⛔ IT DOES NOT COMPILE `src/fw_main.cpp`, AND MUST NEVER BE DESCRIBED AS IF IT DID. No host build can compile
#     that TU. What it compiles is the two caller REGIONS, lifted verbatim by unique anchors (the
#     `tools/probe_console_sink/ble_guard.py` idiom: extract-or-refuse, never re-type), against the REAL
#     `firmware_commands.cpp`, the REAL `lib/core`/`lib/console`, the REAL `GuardedConsole` and the REAL
#     `LineSink`. The 0c WIRING authority is elsewhere and stays elsewhere:
#       · the seam itself is EXECUTED by `probe_main.cpp`'s X-rows (both format arms, real sinks), and
#       · each `fw_main.cpp` adapter is PINNED STRUCTURALLY by `tools/probe_console_sink/structural.py`.
#     This file is the third, different instrument: the auditable byte/side-effect ledger of the move.
#
# ★ WHY THE ANCHORS ARE WHAT THEY ARE (and why there is no marker comment in production source):
#     · serial — everything between `line[pos] = '\0';` and the `pos = 0;` that closes the newline arm. Both the
#       pre-0c and the post-0c shapes live exactly there, so ONE anchor pair reads both revisions.
#     · BLE    — everything after the LAST direct handler (`del_msg`) to the end of `ble_dispatch_line`. Same
#       property: the pre-0c parse-first tail and the post-0c adapter both occupy that region.
#   Every anchor is unique-or-REFUSE. A missing or duplicated anchor stops the tool; it never silently measures a
#   region it guessed at ([[B82]]'s lesson in a different door).
#
# ★ THE MATRIX IS DERIVED, NEVER TYPED (R-RA-1). `tools/gen_command_inventory.py` classifies the real dispatchers;
#   this file projects the compiled profile's primary verbs and qualifying sub-verbs out of those rows and then adds
#   the brief's explicit boundaries. A verb that appears in source therefore appears here without anyone editing a
#   list — which is the property a hand-written corpus cannot have.
#
# ★★ [[B487]] IT BUILDS ONLY THROUGH THE INBOX RUNNER'S OWN BUILDER — `run.sh`'s text before its unique
#   `rc=0` / `if ! build_support;` boundary, the prefix `tools/probe_deferred_actions/run.py` already consumes — with
#   the profile's ARM, so the real `device_ble.h`, the remote-action and client TUs, the arm's defines and both
#   `--wrap` flags are the runner's, never a second recipe here (U1). Two profiles: `full_headless` (arm `accept`,
#   the default) and `mobile` (arm `client`).
# ★★ ONE VALIDITY CONTRACT FOR RUNS AND COMPARISONS: a ledger is valid only against the EXPECTED MATRIX this tool
#   derives — without compiling — for the ledger's declared profile from the current inventory; a ledger's own count
#   is checked against that matrix and never trusted in its place. `--compare A B` validates both and compares only
#   validated records, LINE to LINE, SER to SER, BLE to BLE: exit 0 identical, 1 different, 2 refused.
#
# USAGE
#   tools/probe_inbox_verbs/transcript.py --fw-main src/fw_main.cpp --out <dir>    # full_headless ledger on stdout
#   tools/probe_inbox_verbs/transcript.py --profile mobile --emit mobile.txt        # the mobile profile
#   tools/probe_inbox_verbs/transcript.py --fw-main <(git show 106c6b8:src/fw_main.cpp) --label pre0c --emit a.txt
#   tools/probe_inbox_verbs/transcript.py --compare a.txt b.txt                     # validate both, compare records
#   tools/probe_inbox_verbs/transcript.py --print-matrix [--profile mobile]        # the derived matrix only
"""Extract both fw_main.cpp caller shapes, run the derived line matrix through them, print the byte transcript;
or validate and compare two transcripts."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shlex
import subprocess
import sys
import tempfile
from typing import NamedTuple

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gen_command_inventory as GEN   # noqa: E402 — the ONE classification authority (R-RA-1), never a second scan

# The probe's compilation arm is `[env:heltec_v3]`'s DEFS (see run.sh) — and its resolved macro set is the
# generator's `full_headless` profile, NOT `full_oled`: `MR_FEAT_OLED` is an explicit per-env `-D` that this arm
# does not pass. ⛔ NOT ASSUMED — the driver prints its own compiled macros and `verify_profile()` below refuses a
# disagreement, which is how this very mistake was caught.
PROBE_PROFILE = "full_headless"
# The supported transcript profiles and the inbox runner's ARM each one builds ([[B487]] B2). ⛔ The OLED and
# identity arms are not transcript profiles: their probes are other drivers.
PROFILE_ARMS = {"full_headless": "accept", "mobile": "client"}


class ExtractError(RuntimeError):
    """A refusal. An un-extractable region must stop the tool, never degrade into a partial measurement."""


# ---------------------------------------------------------------------------------------------------------------
# Extraction
# ---------------------------------------------------------------------------------------------------------------

SERIAL_START = "line[pos] = '\\0';"
SERIAL_END_RE = re.compile(r"^\s*pos = 0;\s*$")
BLE_START_RE = re.compile(r'!strncmp\(line, "del_msg",\s+7\)')


def _unique_line(lines, pred, what):
    hits = [i for i, ln in enumerate(lines) if pred(ln)]
    if len(hits) != 1:
        raise ExtractError("expected exactly ONE %s anchor line, found %d" % (what, len(hits)))
    return hits[0]


def extract_regions(text: str) -> dict:
    """-> {'serial': str, 'ble': str}. Unique-anchor-or-refuse, on the RAW text (the regions are copied verbatim)."""
    lines = text.split("\n")

    start = _unique_line(lines, lambda ln: SERIAL_START in ln, "serial start")
    end = None
    for i in range(start + 1, len(lines)):
        if SERIAL_END_RE.match(lines[i]):
            end = i
            break
    if end is None:
        raise ExtractError("no `pos = 0;` closes the serial newline arm after the start anchor")
    serial = "\n".join(lines[start + 1:end])
    if not serial.strip():
        raise ExtractError("the extracted serial region is EMPTY")

    bstart = _unique_line(lines, lambda ln: BLE_START_RE.search(ln), "BLE start")
    bend = None
    for i in range(bstart + 1, len(lines)):
        if lines[i] == "}":                      # the closing brace of ble_dispatch_line, at column 0
            bend = i
            break
    if bend is None:
        raise ExtractError("no column-0 `}` closes ble_dispatch_line after the start anchor")
    ble = "\n".join(lines[bstart + 1:bend])
    if not ble.strip():
        raise ExtractError("the extracted BLE region is EMPTY")

    # Fail loud on a region that plainly is not the thing we came for.
    if "parse_command" not in serial and "exec_console_line" not in serial:
        raise ExtractError("the serial region contains neither `parse_command` nor `exec_console_line`")
    if "parse_command" not in ble and "exec_console_line" not in ble:
        raise ExtractError("the BLE region contains neither `parse_command` nor `exec_console_line`")
    return {"serial": serial, "ble": ble}


SERVICE_CONSOLE_RE = re.compile(r"^static void service_console\(\)\s*\{\s*$")
USB_ARRAY_RE = re.compile(r"^\s*static\s+char\s+line\[([^\[\];]+)\];")
USB_ARRAY_ANY_RE = re.compile(r"\bchar\b[^;(]*\bline\s*\[")


def usb_line_extent(text: str) -> str:
    """-> the VERBATIM extent of `service_console`'s intake array ([[B488]] B4), refusing a missing, duplicated or
    reshaped declaration. The extracted serial tail limits a line with `sizeof(line) - 1`, so the generated wrapper
    must hand it that array's TYPE — a pointer parameter made the limit the pointer's 7 and refused every longer row."""
    lines = text.split("\n")
    start = _unique_line(lines, lambda ln: SERVICE_CONSOLE_RE.match(ln), "service_console definition")
    end = next((i for i in range(start + 1, len(lines)) if lines[i] == "}"), None)
    if end is None:
        raise ExtractError("no column-0 `}` closes service_console")
    body = lines[start + 1:end]
    declared = [ln for ln in body if USB_ARRAY_ANY_RE.search(ln)]
    exact = [m.group(1).strip() for m in (USB_ARRAY_RE.match(ln) for ln in declared) if m]
    if len(declared) != 1 or len(exact) != 1:
        raise ExtractError("expected exactly ONE `static char line[<extent>];` in service_console, found %d "
                           "declaration(s) of `line`, %d in that exact shape" % (len(declared), len(exact)))
    if not start < _unique_line(lines, lambda ln: SERIAL_START in ln, "serial start") < end:
        raise ExtractError("the serial region does not lie inside service_console — its `line` is another array")
    return exact[0]


def shape_of(regions: dict) -> str:
    """`pre0c` (each caller open-codes the fork) or `post0c` (each caller makes ONE seam call)."""
    n_seam = sum(r.count("exec_console_line(") for r in regions.values())
    n_fork = sum(r.count("parse_command(") + r.count("g_node.on_command(") for r in regions.values())
    if n_seam == 2 and n_fork == 0:
        return "post0c"
    if n_seam == 0 and n_fork > 0:
        return "pre0c"
    raise ExtractError("the two regions are in neither shape (seam calls=%d, open-coded fork calls=%d)"
                       % (n_seam, n_fork))


# ---------------------------------------------------------------------------------------------------------------
# The matrix — DERIVED from the classified inventory, then extended with the brief's explicit boundaries
# ---------------------------------------------------------------------------------------------------------------

# ⛔ Every entry below is a BOUNDARY the brief names, and each one is here because a projection of the verb list
#   cannot produce it: a malformed argument, an unknown verb, an empty line, a help spelling, an over-long line.
BOUNDARY_LINES = [
    # the three send verbs, well-formed (parser-owned, executed) and malformed (parser-owned, refused)
    ('send 5 "hi"',                     "send: canonical unicast by id"),
    ('send 0x11223344 "hi" -a',         "send: by hash, ack requested"),
    ('send 0x11223344 "hi" -e',         "send: by hash, sealed"),
    ('send',                            "send: malformed (no target, no body)"),
    ('send 5 unquoted',                 "send: malformed (unquoted body)"),
    ('send_channel 3 "hi"',             "send_channel: canonical"),
    ('send_channel 3 "hi" -t',          "send_channel: team plane"),
    ('send_channel',                    "send_channel: malformed"),
    ('send_layer 0x11223344 2,3 "hi"',  "send_layer: canonical cross-layer"),
    ('send_layer 0x11223344',           "send_layer: malformed (no path, no body)"),
    # the two peer-book verbs, success and the two NAMED malformed envelopes
    ("peerkey " + "aa" * 32,            "peerkey: canonical 64-hex"),
    ('peerkey ' + "aa" * 32 + ' "bob"', "peerkey: canonical + optional quoted name"),
    ("peerkey zz",                      "peerkey: malformed -> the named peerkey_err envelope"),
    ('peername 0xaaaaaaaa "bob"',       "peername: canonical"),
    ("peername 0xaaaaaaaa",             "peername: malformed -> the named peer_name_err envelope"),
    # reqpubkey: the accepted form carries a BLE-only event and a serial-only hint
    ("reqpubkey 0x11223344",            "reqpubkey: by hash"),
    ("reqpubkey 7",                      "reqpubkey: by id (plane AUTO)"),
    ("reqpubkey 7 -t",                   "reqpubkey: by id, team plane"),
    ("reqpubkey",                        "reqpubkey: malformed"),
    ("resolve 0x11223344",               "resolve: canonical"),
    ("resolve 0x11223344 hard",          "resolve: hard flood"),
    ("resolve nope",                     "resolve: malformed"),
    # router-owned, successful and malformed
    ("routes",                           "router-owned, streaming"),
    ("peers all",                        "router-owned sub-verb"),
    ("peers bogus",                      "router-owned, malformed tail (loud refusal)"),
    ("route add 5 6 1",                  "router-owned sub-verb with arguments"),
    ("route",                            "router-owned prefix near-miss (needs a space)"),
    # ownership boundaries
    ("",                                 "EMPTY input"),
    ("   ",                              "whitespace-only input (ParseErr::empty)"),
    ("zzz_unknown_verb",                 "unknown input"),
    ("help",                             "the help family: bare"),
    ("help messaging",                   "the help family: a retired topic word"),
    ("?",                                "the help family: the `?` alias"),
    ("helpful",                          "NOT help — the prefix near-miss that must fall through"),
    ("HELP",                             "NOT help — wrong case"),
    # length boundaries. ⓘ Each transport stores a line in a fixed array — USB `service_console`'s
    #   `line[local_command_max_bytes + 1]` (1,024 B, 1,023 admitted), BLE `mrble::kLineStorageBytes` in
    #   `device_ble.h` (275 B, 274 admitted) — and each cap is enforced TWICE: by the byte-by-byte INTAKE loops,
    #   OUTSIDE these regions and not exercised here, and by the limit each extracted region hands the seam
    #   (`sizeof(line) - 1` on USB — which is why the USB tail takes the real array, [[B488]] — and
    #   `mrble::kLineStorageBytes - 1` on BLE). These rows put that executed limit on the record at and past each cap.
    ('send 5 "' + "x" * 200 + '"',       "long line (~211 B): inside every cap"),
    ('send 5 "' + "x" * 260 + '"',       "long line (~271 B): past the BLE 256-B direct-reply buffer"),
    ("z" * 300,                          "long unknown line (300 B): past the BLE line store"),
    ("z" * 1023,                         "long unknown line (1,023 B): the USB admitted maximum"),
    ("12345678",                         "eight-byte unknown line: one past a pointer parameter's 7 ([[B488]])"),
]


def derive_matrix(profile: str = PROBE_PROFILE):
    """-> [(id, line, why)] — the inventory projection first, the named boundaries second."""
    rows, _notes, _values, _retests = GEN.build_rows(ROOT)
    macros = GEN.PROFILES[profile]
    surfaces = {(s.file, s.func): s for s in GEN.SURFACES}
    out, seen = [], set()

    def add(line, why):
        if line in seen:
            return
        seen.add(line)
        out.append((line, why))

    # (1) every classified primary verb, and (2) every qualifying sub-verb of a compiled arm. Ordered
    #     deterministically (bytewise) so two runs of this tool are comparable line for line.
    projected = []
    for r in rows:
        key = tuple(r.surface.split("::", 1))
        s = surfaces.get(key)
        if s is None or s.kind not in ("top", "sub"):
            continue
        if "serial" not in [t.strip() for t in r.transports.split(",")]:
            continue
        if r.gate != "—" and not GEN.eval_gate(r.gate, macros):
            continue
        for spelling in GEN._spellings(r.verb):
            if not GEN._WORD_NAME_RE.match(spelling.split()[0]):
                continue
            projected.append((spelling, r.subverb))
    for spelling, subverb in sorted(set(projected), key=lambda t: (t[0].encode(), t[1].encode())):
        add(spelling, "inventory: primary verb")
        if subverb != "—":
            add("%s %s" % (spelling, subverb), "inventory: qualifying sub-verb")
    if not projected:
        raise ExtractError("the inventory projection produced NO verbs — refusing a vacuous matrix")

    n_projected = len(out)
    for line, why in BOUNDARY_LINES:
        add(line, "boundary: " + why)
    return [("L%03d" % (i + 1), line, why) for i, (line, why) in enumerate(out)], n_projected


# ---------------------------------------------------------------------------------------------------------------
# Code generation + run
# ---------------------------------------------------------------------------------------------------------------

HEADER = '''// GENERATED by tools/probe_inbox_verbs/transcript.py — NOT A COMMITTED FILE, NOT A FROZEN SNAPSHOT.
// The two regions below were lifted VERBATIM from %(src)s (sha256 %(sha)s, shape %(shape)s) by unique anchors.
// Re-derive at any time; never hand-edit.
#pragma once
#define MR0C_ADAPTERS_ID "%(shape)s/%(sha12)s"

// The BLE transport sink, in `fw_main.cpp`'s own shape (`static void ble_sink(const char*, size_t)`), with
// `mrble::tx_line` replaced by the probe's capture — the exact substitution `route_ble()` already makes.
static void ble_sink(const char* s, size_t n) { ble_capture(s, n); }

// `fw_main.cpp` reaches these through its `using mrfw::…` block (fw_main.cpp:50-70); reproduced so the extracted
// text compiles UNCHANGED rather than being edited into qualified form.
using mrfw::dispatch;
using mrfw::handle_peerkey;
using mrfw::handle_peername;
using mrfw::print_reqpubkey_hint;

// ★ [[B488]] THE USB INTAKE ARRAY'S TYPE, lifted verbatim from `service_console`'s `static char line[%(extent)s];`.
//   The serial tail computes its limit as `sizeof(line) - 1`, so it receives a REFERENCE to that array; the driver
//   copies each row into a local array of this type.
typedef char Mr0cUsbLine[%(extent)s];

static void mr0c_serial_tail(const char (&line)[%(extent)s], size_t pos) {
%(serial)s
}

static size_t mr0c_ble_tail(const char* line, size_t len, char* out, size_t cap) {
    using namespace meshroute::console;   // ble_dispatch_line's own first statement (fw_main.cpp:454)
%(ble)s
}

struct Mr0cLine { const char* id; const char* line; };
static const Mr0cLine MR0C_MATRIX[] = {
%(matrix)s
};
'''


def _c_lit(s: str) -> str:
    out = ['"']
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif 0x20 <= ord(ch) <= 0x7e:
            out.append(ch)
        else:
            out.append("\\x%02x" % ord(ch))
    out.append('"')
    return "".join(out)


def generate(fw_main_path: str, out_dir: str, profile: str = PROBE_PROFILE):
    with open(fw_main_path, "r", encoding="utf-8") as fh:
        text = fh.read()
    regions = extract_regions(text)
    shape = shape_of(regions)
    extent = usb_line_extent(text)
    sha = hashlib.sha256(text.encode("utf-8")).hexdigest()
    matrix, n_projected = derive_matrix(profile)
    rows = "\n".join('    { "%s", %s },' % (cid, _c_lit(line)) for cid, line, _why in matrix)
    hdr = HEADER % dict(src=os.path.abspath(fw_main_path), sha=sha, sha12=sha[:12], shape=shape, extent=extent,
                        serial=regions["serial"], ble=regions["ble"], matrix=rows)
    path = os.path.join(out_dir, "mr0c_adapters.h")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(hdr)
    return path, shape, sha, matrix, n_projected


# ★ DETERMINISM, AND IT IS A FIX RATHER THAN TIDINESS. `firmware_commands.cpp` defines
# `kBuildStamp[] = __DATE__ " " __TIME__`, and the `version` row prints it — so two runs a minute apart produced
# two different transcripts and the BEFORE/AFTER diff reported a difference the refactor did not cause. GCC honours
# `SOURCE_DATE_EPOCH` for both builtin macros, so the same value `tools/measure_board.py` pins for its board
# artifacts (FIXED_SOURCE_DATE_EPOCH) pins this comparator too. ⛔ NOT normalised out of the transcript afterwards:
# a post-hoc substitution can hide a real change in the same field.
FIXED_SOURCE_DATE_EPOCH = "946684800"


def build_env():
    env = dict(os.environ)
    env["SOURCE_DATE_EPOCH"] = FIXED_SOURCE_DATE_EPOCH
    env["LC_ALL"] = "C"
    env["TZ"] = "UTC"
    return env


# ---------------------------------------------------------------------------------------------------------------
# The build — ONLY through the inbox runner's own builder ([[B487]] B1)
# ---------------------------------------------------------------------------------------------------------------

RUNNER = os.path.join(HERE, "run.sh")
DRIVER = os.path.join(HERE, "transcript_main.cpp")
BUILDER_BOUNDARY = "rc=0\nif ! build_support;"


def builder_prefix(runner_text: str) -> str:
    """`run.sh`'s text before its unique `rc=0` / `if ! build_support;` boundary: its `build_support`,
    `build_variant`, per-arm `DEFS`, `INCS`, `STD` and `LDWRAP`. A missing or duplicated boundary refuses."""
    n = runner_text.count(BUILDER_BOUNDARY)
    if n != 1:
        raise ExtractError("expected exactly ONE `rc=0` / `if ! build_support;` builder boundary in run.sh, found %d"
                           % n)
    return runner_text[:runner_text.index(BUILDER_BOUNDARY)]


def builder_appended(shadow_dir: str, driver: str, binary: str) -> list:
    """EXACTLY what this tool appends after the prefix, and nothing else:
      · `build_support`, then `build_variant` with the real router and handler, the default remote-action and client
        TUs and the driver in the probe slot — the generated header's directory is the shadow-include argument;
      · the driver's own `-Werror` compile (`build_variant` compiles the probe slot without it), with the prefix's
        STD, DEFS and INCS, the header's directory and the three suppressions `run.sh` uses for the identity driver;
      · a copy of the binary out of the prefix's `$OUT`, which the prefix removes on exit."""
    q = shlex.quote
    return [
        "build_support || { echo 'TRANSCRIPT BUILD: build_support failed'; exit 2; }",
        'build_variant "$FW_CMDS" "$FW_INBOX" %s "$OUT/transcript" %s'
        ' || { echo "TRANSCRIPT BUILD: build_variant failed:"; cat "$OUT/build.log"; exit 2; }'
        % (q(shadow_dir), q(driver)),
        '"$CXX" "${STD[@]}" -Wall -Wextra -Werror -Wno-volatile -Wno-deprecated-declarations -Wno-unused-function'
        ' -I%s "${DEFS[@]}" "${INCS[@]}" -c %s -o "$OUT/transcript_werror.o" 2>"$OUT/werror.log"'
        ' || { echo "TRANSCRIPT BUILD: the driver -Werror compile failed:"; cat "$OUT/werror.log"; exit 2; }'
        % (q(shadow_dir), q(driver)),
        'cp "$OUT/transcript" %s || exit 2' % q(binary),
    ]


class Built(NamedTuple):
    binary: str
    header: str
    shape: str
    sha: str
    matrix: list
    n_projected: int
    script: str              # the whole bash script: the runner's prefix + `appended`
    appended: list
    log: bytes               # the builder's stdout + stderr, also written to <out>/build.log


def build(fw_main_path: str, out_dir: str, profile: str, driver: str = DRIVER) -> Built:
    """Generate the header into a fresh directory of its own, then build `driver` with the profile's ARM through the
    runner's builder. ⛔ Refuses (ExtractError) on any builder failure, quoting its log — a binary is never run
    after a refused build."""
    with open(RUNNER, "r", encoding="utf-8") as fh:
        prefix = builder_prefix(fh.read())
    out_dir = os.path.abspath(out_dir)       # ⛔ the prefix `cd`s into its own directory: every path it gets is absolute
    shadow = tempfile.mkdtemp(prefix="mr0c-shadow-", dir=out_dir)     # the generated header, and no other header
    hdr, shape, sha, matrix, n_projected = generate(fw_main_path, shadow, profile)
    binary = os.path.join(out_dir, "transcript.bin")
    appended = builder_appended(shadow, os.path.abspath(driver), binary)
    script = prefix + "\n" + "\n".join(appended) + "\n"
    with open(os.path.join(out_dir, "build.sh"), "w", encoding="utf-8") as fh:
        fh.write(script)
    env = build_env()
    env["MR_PROBE_ARM"] = PROFILE_ARMS[profile]
    r = subprocess.run(["bash", "-c", script, RUNNER], cwd=ROOT, env=env, capture_output=True)
    log = r.stdout + r.stderr
    with open(os.path.join(out_dir, "build.log"), "wb") as fh:
        fh.write(log)
    if r.returncode != 0:
        raise ExtractError("the inbox runner's builder refused (exit %d; %s):\n%s"
                           % (r.returncode, os.path.join(out_dir, "build.log"),
                              log.decode("utf-8", "backslashreplace")[-4000:]))
    return Built(binary, hdr, shape, sha, matrix, n_projected, script, appended, log)


class Result(NamedTuple):
    code: int                # 0 = a valid ledger; 2 = refused
    ledger: str              # the validated ledger, or "" when refused
    messages: list           # for the tool's stderr
    built: object            # the Built, when the build got that far
    child_rc: object         # the driver's exit status, when it ran


def unsupported_profile(profile) -> str:
    return ("TRANSCRIPT REFUSED: unsupported --profile %r — the supported profiles are %s"
            % (profile, " and ".join(sorted(PROFILE_ARMS))))


def run_profile(fw_main_path: str, out_dir: str, profile: str, label: str = "", driver: str = DRIVER) -> Result:
    """Build, run and VALIDATE one ledger (B7's run path). The ledger is the driver's stdout under the tool's head;
    its stderr is recorded separately (<out>/transcript.stderr). Never prints — `main` does."""
    if profile not in PROFILE_ARMS:
        return Result(2, "", [unsupported_profile(profile)], None, None)
    try:
        built = build(fw_main_path, out_dir, profile, driver)
    except ExtractError as exc:
        return Result(2, "", ["TRANSCRIPT REFUSED: %s" % exc], None, None)
    run = subprocess.run([built.binary], capture_output=True, env=build_env())
    err_path = os.path.join(out_dir, "transcript.stderr")
    with open(err_path, "wb") as fh:
        fh.write(run.stderr)
    messages = ["# driver stderr: %d byte(s) -> %s" % (len(run.stderr), err_path)]
    if run.returncode != 0:
        how = ("killed by signal %d" % -run.returncode) if run.returncode < 0 else "exit %d" % run.returncode
        return Result(2, "", messages + ["TRANSCRIPT REFUSED: the transcript driver ended with %s — no ledger:\n%s"
                                         % (how, run.stderr.decode("utf-8", "backslashreplace")[-2000:])],
                      built, run.returncode)
    head = ("# transcript label=%s shape=%s profile=%s fw_main=%s sha256=%s rows=%d (projected=%d boundaries=%d)"
            % (label or "-", built.shape, profile, os.path.abspath(fw_main_path), built.sha, len(built.matrix),
               built.n_projected, len(built.matrix) - built.n_projected))
    try:
        if "\n" in head or "\r" in head:
            raise LedgerError("the head would span lines (a label or path holds a line break)")
        ledger = head + "\n" + ascii_text(run.stdout, "the driver's stdout")
        validate_ledger(ledger, "the fresh ledger")
    except LedgerError as exc:
        return Result(2, "", messages + ["TRANSCRIPT REFUSED: INVALID LEDGER — %s" % exc], built, 0)
    return Result(0, ledger, messages, built, 0)


def verify_profile(text: str, profile: str) -> None:
    """The macros the BINARY compiled must be the profile the MATRIX was projected for, or the run is refused."""
    m = re.search(r"^MR0C-PROFILE (.+)$", text, re.M)
    if not m:
        raise ExtractError("the driver printed no MR0C-PROFILE line — the compiled macro set is unknown")
    got = dict(kv.split("=", 1) for kv in m.group(1).split())
    want = {k: str(v) for k, v in GEN.PROFILES[profile].items()}
    got = {k: v for k, v in got.items() if k in want}
    if got != want:
        raise ExtractError("compiled macros %r are not profile %r %r — the matrix would project the wrong surface"
                           % (got, profile, want))


# ---------------------------------------------------------------------------------------------------------------
# The ledger — ONE validity contract for runs and comparisons ([[B487]] B7, B8; review TPR-1)
# ---------------------------------------------------------------------------------------------------------------

class LedgerError(ExtractError):
    """An INVALID ledger: refused (exit 2), naming its first violation — never emitted or compared as valid."""


_ESC = r"(?:[\x20-\x5b\x5d-\x7e]|\\[rn\\]|\\x[0-9a-f]{2})*"       # the driver's `esc()` output, and nothing else
_FP = (r"ctr\+-?\d+ txq=\d+ txdrop=\d+ rt=\d+ hw_dm\+\d+ hw_ch\+\d+ wipes\+\d+ nvw\+-?\d+ kh=[0-9a-f]{8} pend=\d+ "
       r"recs=\d+/\d+ routed=\S+")                                   # every `print_fp` field, in its order
IDENT_RE = re.compile(r"MR0C-TRANSCRIPT v1 adapters=(?:pre0c|post0c)/[0-9a-f]{12} lines=(\d+)")
LINE_RE = re.compile(r"LINE (L\d{3,}) len=(\d+) \[(%s)\]" % _ESC)
SER_RE = re.compile(r"  SER usb=\[(%s)\] ble=\[(%s)\] fp=(%s)" % (_ESC, _ESC, _FP))
BLE_RE = re.compile(r"  BLE ret=(\d+) buf=\[(%s)\] nus=\[(%s)\] usb=\[(%s)\] fp=(%s)" % (_ESC, _ESC, _ESC, _FP))
END_RE = re.compile(r"MR0C-TRANSCRIPT END chk=-?\d+ fail=-?\d+")
SER_FIELDS = ("usb", "ble", "fp")
BLE_FIELDS = ("ret", "buf", "nus", "usb", "fp")


class Row(NamedTuple):
    id: str
    length: int
    command: bytes           # the LINE's bracketed text, decoded by the driver's escape rule
    escaped: str             # ... and as printed
    ser: tuple               # SER_FIELDS, as printed
    ble: tuple               # BLE_FIELDS, as printed


class Ledger(NamedTuple):
    profile: str
    head: str
    identity: str
    declared: int
    rows: list
    end: str


def unescape_ledger(text: str) -> bytes:
    """Invert the driver's `esc()` (`\\r`, `\\n`, `\\\\`, `\\xHH`) on text the grammar has already admitted."""
    out, i = bytearray(), 0
    while i < len(text):
        if text[i] != "\\":
            out.append(ord(text[i]))
            i += 1
        elif text[i + 1] in "rn\\":
            out.append({"r": 13, "n": 10, "\\": 92}[text[i + 1]])
            i += 2
        else:
            out.append(int(text[i + 2:i + 4], 16))
            i += 4
    return bytes(out)


def ascii_text(data: bytes, origin: str) -> str:
    try:
        return data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise LedgerError("%s: byte 0x%02x at offset %d is not ASCII — the driver escapes every byte"
                          % (origin, data[exc.start], exc.start))


def _describe(line: str) -> str:
    for prefix, what in (("MR0C-TRANSCRIPT v1 ", "an identity line"), ("MR0C-PROFILE", "a profile line"),
                         ("MR0C-TRANSCRIPT END", "an END line"), ("LINE ", "a LINE record"),
                         ("  SER ", "a SER record"), ("  BLE ", "a BLE record"), ("# transcript", "a head line")):
        if line.startswith(prefix):
            return "%s (%.100s)" % (what, line)
    return "an unexpected line %.100r" % line


def parse_ledger(text: str, origin: str) -> Ledger:
    """The ledger GRAMMAR, in order: head, one identity line, one profile line (checked by `verify_profile`), then
    LINE/SER/BLE triples, then one END as the last line — and no other line. Raises LedgerError on the first
    violation. ⛔ This checks shape only: whether the rows ARE the expected matrix is `match_expected`'s question."""
    def fail(n, why):
        raise LedgerError("%s, line %d: %s" % (origin, n, why))

    def count(n, digits, what):
        # ⛔ QA TQ-1 ([[B491]]): a count is read only once it is SHAPED like one. An unbounded digit run reached
        #   Python's int-conversion limit and escaped as a ValueError (exit 1) instead of this named refusal.
        if len(digits) > 9:
            fail(n, "%s has %d digits — not a count a ledger can carry" % (what, len(digits)))
        return int(digits)

    if not text.endswith("\n") or "\r" in text:
        fail(text.count("\n") + 1, "a ledger is LF-terminated lines with no carriage return")
    lines = text[:-1].split("\n")
    if not lines[0].startswith("# transcript "):
        fail(1, "the first line is not the tool's `# transcript` head")
    profiles = [tok[len("profile="):] for tok in lines[0].split() if tok.startswith("profile=")]
    if len(profiles) != 1:
        fail(1, "the head carries %d `profile=` field(s), not exactly one" % len(profiles))
    if profiles[0] not in PROFILE_ARMS:
        fail(1, "the head declares profile %r; the supported profiles are %s"
             % (profiles[0], " and ".join(sorted(PROFILE_ARMS))))
    ident = IDENT_RE.fullmatch(lines[1]) if len(lines) > 1 else None
    if not ident:
        fail(2, "expected the identity line `MR0C-TRANSCRIPT v1 adapters=<shape>/<12 hex> lines=<N>`, found %s"
             % (_describe(lines[1]) if len(lines) > 1 else "the end of the ledger"))
    if len(lines) < 3 or not lines[2].startswith("MR0C-PROFILE "):
        fail(3, "expected the MR0C-PROFILE line, found %s" % (_describe(lines[2]) if len(lines) > 2 else "nothing"))
    # ⛔ QA TQ-1 ([[B491]]): `verify_profile` (unchanged) reads every field as `name=value`; a field without `=` is
    #   refused HERE, by name, rather than escaping it as a ValueError. Which names and values are right stays its call.
    bare = [field for field in lines[2].split()[1:] if "=" not in field]
    if bare:
        fail(3, "the MR0C-PROFILE line carries a field that is not `name=value`: %r" % bare[0][:60])
    try:
        verify_profile(lines[2], profiles[0])
    except ExtractError as exc:
        fail(3, str(exc))
    rows, n = [], 3
    while n < len(lines) and lines[n].startswith("LINE "):
        m = LINE_RE.fullmatch(lines[n])
        if not m:
            fail(n + 1, "malformed LINE record %.120r" % lines[n])
        ser = SER_RE.fullmatch(lines[n + 1]) if n + 1 < len(lines) else None
        if not ser:
            fail(n + 2, "expected the SER record of %s with every field (usb, ble, fp), found %s"
                 % (m.group(1), _describe(lines[n + 1]) if n + 1 < len(lines) else "nothing"))
        ble = BLE_RE.fullmatch(lines[n + 2]) if n + 2 < len(lines) else None
        if not ble:
            fail(n + 3, "expected the BLE record of %s with every field (ret, buf, nus, usb, fp), found %s"
                 % (m.group(1), _describe(lines[n + 2]) if n + 2 < len(lines) else "nothing"))
        command = unescape_ledger(m.group(3))
        length = count(n + 1, m.group(2), "LINE %s's len" % m.group(1))
        if length != len(command):
            fail(n + 1, "LINE %s declares len=%d but carries %d byte(s)" % (m.group(1), length, len(command)))
        rows.append(Row(m.group(1), length, command, m.group(3), ser.groups(), ble.groups()))
        n += 3
    if n >= len(lines) or not END_RE.fullmatch(lines[n]):
        fail(n + 1, "expected `MR0C-TRANSCRIPT END chk=<n> fail=<n>` after the last row, found %s"
             % (_describe(lines[n]) if n < len(lines) else "the end of the ledger"))
    if n != len(lines) - 1:
        fail(n + 2, "END must be the last line, but %s follows it" % _describe(lines[n + 1]))
    return Ledger(profiles[0], lines[0], lines[1], count(2, ident.group(1), "the identity's lines="), rows, lines[n])


_EXPECTED = {}


def expected_matrix(profile: str) -> list:
    """The EXPECTED MATRIX: the ordered `(ID, command bytes)` rows `derive_matrix` produces for `profile` from the
    CURRENT inventory — derived without compiling, never read from a ledger. (Bytes exactly as `_c_lit` spells them.)"""
    if profile not in _EXPECTED:
        _EXPECTED[profile] = [(cid, line.encode("latin-1")) for cid, line, _why in derive_matrix(profile)[0]]
    return _EXPECTED[profile]


def match_expected(ledger: Ledger, expected: list, origin: str) -> None:
    """★★ B7 — THE ONE CHECK that a ledger's rows ARE the expected matrix: its count, and each row's ID, length and
    command bytes, in order. ⛔ Trusting the ledger's own count instead is exactly what let an empty or truncated
    pair compare as identical (review TPR-1)."""
    if ledger.declared != len(expected):
        raise LedgerError("%s: declares lines=%d, but profile %s's expected matrix has %d rows"
                          % (origin, ledger.declared, ledger.profile, len(expected)))
    for k, ((cid, command), row) in enumerate(zip(expected, ledger.rows), 1):
        if (row.id, row.length, row.command) != (cid, len(command), command):
            raise LedgerError("%s: row %d is LINE %s len=%d [%.60s], not the expected %s len=%d [%.60s]"
                              % (origin, k, row.id, row.length, row.escaped, cid, len(command),
                                 command.decode("latin-1")))


def validate_ledger(text: str, origin: str) -> Ledger:
    """B7: the grammar, the declared count against the rows carried, then the expected matrix."""
    ledger = parse_ledger(text, origin)
    if ledger.declared != len(ledger.rows):
        raise LedgerError("%s: declares lines=%d but carries %d row(s)" % (origin, ledger.declared, len(ledger.rows)))
    match_expected(ledger, expected_matrix(ledger.profile), origin)
    return ledger


def compare_ledgers(path_a: str, path_b: str, profile=None):
    """B8: validate both ledgers (same declared profile), then compare ONLY validated records like for like —
    LINE to LINE, SER to SER, BLE to BLE, never SER to BLE. -> (exit code, report lines). Provenance (the heads and
    the adapters identity) is reported, never compared, so two `fw_main.cpp` revisions compare their rows."""
    try:
        sides = []
        for tag, path in (("A", path_a), ("B", path_b)):
            with open(path, "rb") as fh:
                origin = "%s (%s)" % (tag, path)
                sides.append(validate_ledger(ascii_text(fh.read(), origin), origin))
    except (LedgerError, OSError) as exc:
        return 2, ["COMPARE REFUSED: %s" % exc]
    a, b = sides
    if a.profile != b.profile:
        return 2, ["COMPARE REFUSED: A declares profile %s and B declares %s" % (a.profile, b.profile)]
    if profile is not None and profile != a.profile:
        return 2, ["COMPARE REFUSED: --profile %s, but both ledgers declare %s" % (profile, a.profile)]
    report = ["# compare profile=%s rows=%d" % (a.profile, len(a.rows)),
              "# A: %s | %s" % (a.head, a.identity), "# B: %s | %s" % (b.head, b.identity)]
    diffs = []
    for ra, rb in zip(a.rows, b.rows):
        for kind, fields, va, vb in (("LINE", ("line",), (ra.escaped,), (rb.escaped,)),
                                     ("SER", SER_FIELDS, ra.ser, rb.ser), ("BLE", BLE_FIELDS, ra.ble, rb.ble)):
            changed = ["%s: A=[%s] B=[%s]" % (f, x, y) for f, x, y in zip(fields, va, vb) if x != y]
            if changed:
                diffs.append("%s %s" % (ra.id, kind))
                report.append("DIFF %s %s [%s] — %s" % (ra.id, kind, ra.escaped, "; ".join(changed)))
    if not diffs:
        report.append("COMPARE: identical — %d rows, %d records (LINE/SER/BLE), profile %s"
                      % (len(a.rows), 3 * len(a.rows), a.profile))
        return 0, report
    report.append("COMPARE: %d record(s) differ: %s" % (len(diffs), ", ".join(diffs)))
    return 1, report


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fw-main", default=os.path.join(ROOT, "src/fw_main.cpp"))
    ap.add_argument("--profile", default=None,
                    help="full_headless (the default; inbox arm `accept`) or mobile (inbox arm `client`)")
    ap.add_argument("--label", default="")
    ap.add_argument("--out", default=None)
    ap.add_argument("--emit", default=None, help="write the transcript to this file as well as stdout")
    ap.add_argument("--print-matrix", action="store_true")
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"),
                    help="validate two ledgers against the current inventory and compare their records; no build")
    ap.add_argument("--keep", action="store_true", help="keep the generated header/binaries (diagnosis)")
    a = ap.parse_args(argv)

    if a.compare:
        code, report = compare_ledgers(a.compare[0], a.compare[1], a.profile)
        print("\n".join(report), file=sys.stderr if code == 2 else sys.stdout)
        return code

    profile = a.profile or PROBE_PROFILE
    if profile not in PROFILE_ARMS:
        print(unsupported_profile(profile), file=sys.stderr)
        return 2

    if a.print_matrix:
        matrix, n_projected = derive_matrix(profile)
        for cid, line, why in matrix:
            print("%s\t%s\t%s" % (cid, line, why))
        print("# %d rows (%d inventory-projected, %d boundaries) — profile %s"
              % (len(matrix), n_projected, len(matrix) - n_projected, profile))
        return 0

    if a.label and not re.fullmatch(r"[\x21-\x7e]+", a.label):
        print("TRANSCRIPT REFUSED: --label must be one printable-ASCII token (it is a field of the head line)",
              file=sys.stderr)
        return 2
    out = a.out or tempfile.mkdtemp(prefix="mr0c-transcript-")
    os.makedirs(out, exist_ok=True)
    res = run_profile(a.fw_main, out, profile, a.label)
    for message in res.messages:
        print(message, file=sys.stderr)
    if res.code != 0:
        return res.code
    sys.stdout.write(res.ledger)
    if a.emit:
        with open(a.emit, "w", encoding="ascii") as fh:
            fh.write(res.ledger)
    return 0


if __name__ == "__main__":
    sys.exit(main())
