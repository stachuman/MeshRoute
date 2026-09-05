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
# and diff the two transcripts. An empty diff IS the C1 proof; anything else is the refactor's cost, named line by
# line. Because BOTH arms link the SAME `src/firmware_commands.cpp`, the comparison isolates exactly the thing 0c
# changes: the CALLER SHAPE.
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
# USAGE
#   tools/probe_inbox_verbs/transcript.py --fw-main src/fw_main.cpp            # transcript of the current tree
#   tools/probe_inbox_verbs/transcript.py --fw-main <(git show 106c6b8:src/fw_main.cpp) --label pre0c
#   tools/probe_inbox_verbs/transcript.py --print-matrix                       # the derived matrix only
"""Extract both fw_main.cpp caller shapes, run the derived line matrix through them, print the byte transcript."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gen_command_inventory as GEN   # noqa: E402 — the ONE classification authority (R-RA-1), never a second scan

# The probe's compilation arm is `[env:heltec_v3]`'s DEFS (see run.sh) — and its resolved macro set is the
# generator's `full_headless` profile, NOT `full_oled`: `MR_FEAT_OLED` is an explicit per-env `-D` that this arm
# does not pass. ⛔ NOT ASSUMED — the driver prints its own compiled macros and `verify_profile()` below refuses a
# disagreement, which is how this very mistake was caught.
PROBE_PROFILE = "full_headless"


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
    # length boundaries. ⓘ The INTAKE caps (serial 1024, BLE 275) live in `service_console`'s reader loop and in
    #   `device_ble.h`, both OUTSIDE these regions and both untouched by 0c; these rows exercise the executed path
    #   with lines at and past the BLE product cap so the seam's behaviour on a long line is on the record too.
    ('send 5 "' + "x" * 200 + '"',       "long line (~211 B): inside every cap"),
    ('send 5 "' + "x" * 260 + '"',       "long line (~271 B): past the BLE 256-B direct-reply buffer"),
    ("z" * 300,                          "long unknown line (300 B): past the BLE line store"),
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

static void mr0c_serial_tail(const char* line, size_t pos) {
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
    sha = hashlib.sha256(text.encode("utf-8")).hexdigest()
    matrix, n_projected = derive_matrix(profile)
    rows = "\n".join('    { "%s", %s },' % (cid, _c_lit(line)) for cid, line, _why in matrix)
    hdr = HEADER % dict(src=os.path.abspath(fw_main_path), sha=sha, sha12=sha[:12], shape=shape,
                        serial=regions["serial"], ble=regions["ble"], matrix=rows)
    path = os.path.join(out_dir, "mr0c_adapters.h")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(hdr)
    return path, shape, sha, matrix, n_projected


DEFAULT_DEFS = ["-DARDUINO=100", "-DMR_CONSOLE=1", "-DBOARD_HELTEC_V3"]

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


def default_flags(out_dir: str):
    incs = [f"-I{out_dir}", f"-I{HERE}/fakes", f"-I{ROOT}/tools/probe_board_ui/fakes",
            f"-I{ROOT}/tools/probe_device_radio/fakes", f"-I{ROOT}/variants/heltec_common",
            f"-I{ROOT}/src", f"-I{ROOT}/lib/hal", f"-I{ROOT}/lib/core", f"-I{ROOT}/lib/console",
            f"-I{ROOT}/lib/monocypher/src"]
    return ["-std=gnu++20", "-fno-exceptions", "-fno-rtti", "-O0"] + DEFAULT_DEFS + incs


def build_and_run(fw_main_path: str, out_dir: str, cxx="g++", cc="gcc", profile=PROBE_PROFILE):
    hdr, shape, sha, matrix, n_projected = generate(fw_main_path, out_dir, profile)
    flags = default_flags(out_dir)
    objs = []
    sources = ([os.path.join(ROOT, "lib/core", f) for f in sorted(os.listdir(os.path.join(ROOT, "lib/core")))
                if f.endswith(".cpp")]
               + [os.path.join(ROOT, p) for p in ("lib/hal/device_hal.cpp", "lib/hal/timer_wheel.cpp",
                                                  "lib/hal/airtime_ledger.cpp", "lib/console/console_json.cpp",
                                                  "lib/console/console_parse.cpp", "src/firmware_commands.cpp",
                                                  "src/firmware_inbox.cpp")])
    for s in sources:
        o = os.path.join(out_dir, "o_" + os.path.basename(s).replace(".cpp", ".o"))
        r = subprocess.run([cxx, *flags, "-Wall", "-Wextra", "-c", s, "-o", o],
                           capture_output=True, text=True, env=build_env())
        if r.returncode:
            raise ExtractError("support build failed for %s:\n%s" % (s, r.stderr[:2000]))
        objs.append(o)
    o_mono = os.path.join(out_dir, "o_monocypher.o")
    r = subprocess.run([cc, "-std=gnu17", "-O0", f"-I{ROOT}/lib/monocypher/src", "-c",
                        os.path.join(ROOT, "lib/monocypher/src/monocypher.c"), "-o", o_mono],
                       capture_output=True, text=True, env=build_env())
    if r.returncode:
        raise ExtractError("monocypher build failed:\n" + r.stderr[:2000])
    objs.append(o_mono)
    # ⓘ `-Wno-unused-function`: this driver reuses `probe_main.cpp` wholesale (U1) and does not call every fixture
    #   the 71-check `main()` uses. The probe's OWN default build keeps -Werror with no such suppression.
    o_main = os.path.join(out_dir, "o_transcript.o")
    r = subprocess.run([cxx, *flags, "-Wall", "-Wextra", "-Werror", "-Wno-volatile",
                        "-Wno-deprecated-declarations", "-Wno-unused-function",
                        f'-DMR0C_ADAPTERS_HEADER="mr0c_adapters.h"',
                        "-c", os.path.join(HERE, "transcript_main.cpp"), "-o", o_main],
                       capture_output=True, text=True, env=build_env())
    if r.returncode:
        raise ExtractError("transcript driver build failed:\n" + r.stderr[:4000])
    binary = os.path.join(out_dir, "transcript.bin")
    r = subprocess.run([cxx, o_main, *objs, "-o", binary], capture_output=True, text=True, env=build_env())
    if r.returncode:
        raise ExtractError("link failed:\n" + r.stderr[:2000])
    run = subprocess.run([binary], capture_output=True, text=True, env=build_env())
    verify_profile(run.stdout, profile)
    return run.returncode, run.stdout + run.stderr, shape, sha, matrix, n_projected, hdr


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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fw-main", default=os.path.join(ROOT, "src/fw_main.cpp"))
    ap.add_argument("--label", default="")
    ap.add_argument("--out", default=None)
    ap.add_argument("--emit", default=None, help="write the transcript to this file as well as stdout")
    ap.add_argument("--print-matrix", action="store_true")
    ap.add_argument("--keep", action="store_true", help="keep the generated header/binaries (diagnosis)")
    a = ap.parse_args(argv)

    if a.print_matrix:
        matrix, n_projected = derive_matrix()
        for cid, line, why in matrix:
            print("%s\t%s\t%s" % (cid, line, why))
        print("# %d rows (%d inventory-projected, %d boundaries)"
              % (len(matrix), n_projected, len(matrix) - n_projected))
        return 0

    out = a.out or tempfile.mkdtemp(prefix="mr0c-transcript-")
    os.makedirs(out, exist_ok=True)
    try:
        rc, text, shape, sha, matrix, n_projected, _hdr = build_and_run(a.fw_main, out)
    except ExtractError as exc:
        print("TRANSCRIPT REFUSED: %s" % exc)
        return 2
    head = ("# transcript label=%s shape=%s fw_main=%s sha256=%s rows=%d (projected=%d boundaries=%d)"
            % (a.label or "-", shape, os.path.abspath(a.fw_main), sha, len(matrix), n_projected,
               len(matrix) - n_projected))
    body = head + "\n" + text
    sys.stdout.write(body)
    if a.emit:
        with open(a.emit, "w", encoding="utf-8") as fh:
            fh.write(body)
    if rc != 0:
        print("TRANSCRIPT BINARY EXITED %d" % rc)
    return rc


if __name__ == "__main__":
    sys.exit(main())
