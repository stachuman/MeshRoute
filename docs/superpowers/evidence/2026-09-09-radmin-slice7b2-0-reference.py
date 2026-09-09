#!/usr/bin/env python3
"""Independent R-RA-36 vectors; no production codec or primitive is imported.

Reuses the complete, frozen Slice-2 reference embedded in its durable evidence,
including both external primitive anchors and the unchanged 87-vector comparison.
New vectors use explicit concatenation and libsodium, never production output.
"""
import argparse
import contextlib
import hashlib
import io
from pathlib import Path
import re
import sys
import types


def original():
    path = Path(__file__).with_name("2026-09-06-radmin-slice2.md")
    text = path.read_text(encoding="utf-8")
    source = re.search(r"### 3\.7[^\n]*\n\n```python\n(.*?)\n```", text, re.S).group(1)
    module = types.ModuleType("slice2_reference")
    exec(compile(source, str(path) + ":3.7", "exec"), module.__dict__)
    module.run_anchors()
    module.build()
    module.report_nonce_matrix()
    print("Original reference source SHA256:", hashlib.sha256((source + "\n").encode()).hexdigest())
    return module


def vectors(ref):
    # Positive combinations: every permitted request class; every established slot;
    # busy wire extrema; zero source and ID. Invalid tuples are authenticated below.
    valid = [(3, op | 3, code, detail, ref.REQ_ID, ref.SRC_HASH)
             for code, ops, detail in [(0, [0], 0), (1, [0, 0x40, 0x50], 0),
                                       (2, [0x40], 1), (3, [0x40, 0x50], 0),
                                       (4, [0x40, 0x50], 0)] for op in ops]
    valid += [(slot, slot, 0, 0, ref.REQ_ID, ref.SRC_HASH) for slot in range(10)]
    valid += [(3, 0x43, 2, detail, ref.REQ_ID, ref.SRC_HASH) for detail in (2, 4, 255)]
    valid += [(3, 3, 0, 0, req, src) for req, src in
              [(0, ref.SRC_HASH), (ref.REQ_ID, 0), (0, 0)]]

    def seal(slot, request, code, detail, req=ref.REQ_ID, src=ref.SRC_HASH):
        ctl = bytes([0x50 | slot])
        notice = bytes([request, code, detail])
        header = ctl + req.to_bytes(8, "little") + notice
        preimage = (b"MeshRoute remote-admin v2 nonce" + ref.SESSION_KEY + b"\xa1" + ctl
                    + req.to_bytes(8, "little") + b"\x00" + src.to_bytes(4, "little") + notice)
        nonce = hashlib.blake2b(preimage, digest_size=64).digest()[:24]
        aad = b"\xa1" + header + src.to_bytes(4, "little")
        tag = ref.xseal(b"", aad, nonce, ref.SESSION_KEY)
        assert len(tag) == 16
        return header, nonce, aad, tag, header + tag

    positive = b"".join(b"".join(seal(*row)) for row in valid)
    # Every code byte, request-ctl byte, nonzero unused detail, and slot pairing.
    # These invalid inputs have VALID tags from the independent stack. Decoder
    # tests must get a semantic refusal, never merely auth_failed.
    invalid = [(3, 3, code, 0) for code in range(5, 256)]
    invalid += [(3, request, 0, 0) for request in range(256) if request != 3]
    invalid += [(3, request, code, detail) for code, request in [(0, 3), (1, 3), (3, 0x43), (4, 0x43)]
                for detail in range(1, 256)]
    invalid += [(3, 0x43, 2, 0)]
    invalid += [(3, (op << 4) | 3, code, 1 if code == 2 else 0)
                for code, allowed in [(0, {0}), (1, {0, 4, 5}), (2, {4}), (3, {4, 5}), (4, {4, 5})]
                for op in range(16) if op not in allowed]
    invalid += [(slot, request_slot, 0, 0) for slot in range(10)
                for request_slot in range(16) if request_slot != slot]
    invalid = list(dict.fromkeys(invalid))
    negative = b"".join(seal(*row)[4] for row in invalid)
    print(f"Admission vectors: {len(valid)} positive x 97 bytes; {len(invalid)} valid-tag invalid x 28 bytes")
    return {"kAdmissionRefValid": positive, "kAdmissionRefInvalid": negative}


LITERAL = re.compile(r'const\s+uint8_t\s+(k(?:Admission)?Ref\w+)\s*\[\s*(\d+)\s*\]\s*=\s*\{(.*?)\};', re.S)


def compare(source, expected):
    found = {}
    for name, count, blob in LITERAL.findall(source):
        value = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", blob))
        if name in found or len(value) != int(count):
            raise ValueError(f"duplicate/mis-sized literal: {name}")
        found[name] = value
    problems = []
    for name in sorted(expected.keys() | found.keys()):
        if name not in found:
            problems.append("MISSING " + name)
        elif name not in expected:
            problems.append("EXTRA " + name)
        elif found[name] != expected[name]:
            offset = next((i for i, pair in enumerate(zip(found[name], expected[name])) if pair[0] != pair[1]),
                          min(len(found[name]), len(expected[name])))
            problems.append(f"MISMATCH {name} at byte {offset}")
    if problems:
        raise ValueError("; ".join(problems))
    print(f"Strict comparison: {len(expected)}/{len(found)} literals match")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit", action="store_true")
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    # stdout in emission mode contains ONLY generated C++ (anchor diagnostics stderr).
    with contextlib.redirect_stdout(sys.stderr if args.emit else sys.stdout):
        ref = original()
        new = vectors(ref)
    if args.emit:
        for name, value in new.items():
            print(ref.cpp(name, value))
    if args.compare:
        if ref.compare(str(args.compare)):
            return 1
        source = args.compare.read_text(encoding="utf-8")
        expected = ref.VECTORS | new
        compare(source, expected)
        if args.selftest:
            match = next(m for m in LITERAL.finditer(source) if m.group(1) == "kAdmissionRefValid")
            byte = re.search(r"0x([0-9a-fA-F]{2})", match.group(3))
            at = match.start(3) + byte.start()
            corrupt = source[:at] + f"0x{int(byte.group(1), 16) ^ 1:02x}" + source[at + 4:]
            controls = {"changed": corrupt, "missing": source[:match.start()] + source[match.end():],
                        "extra": source + '\nconst uint8_t kAdmissionRefExtra[1] = {0x00};',
                        "duplicate": source + match.group(0)}
            for label, candidate in controls.items():
                try:
                    compare(candidate, expected)
                except ValueError as error:
                    print(f"COMPARATOR CONTROL RED ({label}): {error}")
                else:
                    raise ValueError("comparator control GREEN: " + label)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ValueError as error:
        sys.exit(str(error))
