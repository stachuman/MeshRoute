#!/usr/bin/env python3
"""Independent R-RA-37 terminal allocation vectors; no production imports.

Extends the frozen Slice-2 and R-RA-36 references without editing either one.
The old terminal code08 bytes become positive; their historical generator stays
unchanged. Alternate terminal plaintexts here are synthetic codec fixtures, not
permission to replace a live transcript under its deterministic nonce.
"""
import argparse
import contextlib
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys


def prior_reference():
    path = Path(__file__).with_name("2026-09-09-radmin-slice7b2-0-reference.py")
    spec = importlib.util.spec_from_file_location("admission_reference", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def additions(ref):
    records = {}
    for suffix, auth, plaintext in [
            ("auth_code09", True, b"\x09"),
            ("open_code08", False, b"\x08"),
            ("open_code09", False, b"\x09"),
            ("auth_code08_detail", True, b"\x08\xd1"),
            ("open_code08_detail", False, b"\x08\xd1")]:
        ctl = bytes([0x13 if auth else 0x1f])
        header = ctl + ref.REQ_ID.to_bytes(8, "little") + bytes([ref.SEQ])
        source = ref.SRC_HASH.to_bytes(4, "little")
        nonce = aad = None
        if auth:
            preimage = (b"MeshRoute remote-admin v2 nonce" + ref.SESSION_KEY + b"\xa1" + header + source)
            nonce = hashlib.blake2b(preimage, digest_size=64).digest()[:24]
            aad = b"\xa1" + header + source
            body = header + ref.xseal(plaintext, aad, nonce, ref.SESSION_KEY)
        else:
            body = header + plaintext
        records["kRefBody_resp_terminal_" + suffix] = dict(
            plaintext=plaintext.hex(), header=header.hex(),
            nonce=nonce.hex() if nonce is not None else None,
            aad=aad.hex() if aad is not None else None, body=body.hex())
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit", action="store_true")
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--freeze-check", action="store_true")
    args = parser.parse_args()
    prior = prior_reference()
    with contextlib.redirect_stdout(sys.stderr if args.emit else sys.stdout):
        # original() runs BOTH external anchors before generating any vector.
        ref = prior.original()
        old = ref.VECTORS | prior.vectors(ref)
        assert len(old) == 89
        records = additions(ref)
        new = {name: bytes.fromhex(row["body"]) for name, row in records.items()}
        expected = old | new
        assert len(expected) == 94
        assert records["kRefBody_resp_terminal_auth_code09"]["nonce"] == ref.VECTORS["kRefNonce_resp_terminal_auth"].hex()
        if args.freeze_check:
            path = Path(__file__).with_name("2026-09-13-radmin-slice7b3-0-precheck") / "independent-boundary-vectors.json"
            frozen = json.loads(path.read_text())
            assert records == frozen["proposed_additions"]
            assert {k: hashlib.sha256(v).hexdigest() for k, v in old.items()} == frozen["existing_array_sha256"]
            print("Frozen independent inputs: five additions and all old 89 hashes match")
        if args.compare:
            source = args.compare.read_text(encoding="utf-8")
            # The complete strict comparison catches all extras/duplicates, even
            # though the separately reported old-89 identity check is a subset.
            prior.compare(source, expected)
            old_source = "\n".join(m.group(0) for m in prior.LITERAL.finditer(source) if m.group(1) in old)
            prior.compare(old_source, old)
            print("Old-89 identity: 89/89 unchanged")
            if args.selftest:
                match = next(m for m in prior.LITERAL.finditer(source)
                             if m.group(1) == "kRefBody_resp_terminal_auth_code09")
                byte = re.search(r"0x([0-9a-fA-F]{2})", match.group(3))
                at = match.start(3) + byte.start()
                controls = {
                    "changed": source[:at] + f"0x{int(byte.group(1), 16) ^ 1:02x}" + source[at+4:],
                    "missing": source[:match.start()] + source[match.end():],
                    "extra": source + "\nconst uint8_t kRefExtra[1] = {0x00};",
                    "duplicate": source + match.group(0),
                    "mis-sized": source[:match.start(2)] + str(int(match.group(2))+1) + source[match.end(2):],
                }
                for label, candidate in controls.items():
                    try:
                        prior.compare(candidate, expected)
                    except ValueError as error:
                        print(f"COMPARATOR CONTROL RED ({label}): {error}")
                    else:
                        raise ValueError("comparator control GREEN: " + label)
    if args.emit:
        for name, value in new.items():
            print(ref.cpp(name, value))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ValueError as error:
        sys.exit(str(error))
