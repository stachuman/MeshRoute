#!/usr/bin/env python3
"""Slice 9: preserve the 94 independent vectors and the last legacy sealer output.

The historical reference is delegated unchanged. The legacy constexpr array is
owned here: the older comparator recognizes const uint8_t arrays only. Its bytes
were captured from 84edd3e's real sealer and reopened with the real old decoder;
see 2026-09-18-radmin-slice9-preflight-r2/capture-legacy.cpp and commands.json.
No production codec or sealer is imported by this checker.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import re
import sys

LEGACY = bytes.fromhex(
    "deadbeef010203040100e2136ffc9b0472121a6259e8bd6ba0af59505d903bccbcf3fb1f515069ea")
LEGACY_SHA256 = "f8cf9ec1971840b136c72745bc692cb3727de6b2f10493c6d87a86a51fb38138"
CASE = re.compile(r'TEST_CASE\("§radmin-2/legacy[^"\n]*"\)\s*\{')
LITERAL = re.compile(r'constexpr\s+uint8_t\s+kRefLegacySealed\s*\[\s*40\s*\]\s*=\s*\{([^}]+)\};')
TOKENS = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/')


def uncomment(source):
    return TOKENS.sub(lambda m: " " * len(m[0]) if m[0].startswith(("//", "/*")) else m[0], source)


def check_legacy(source):
    code = uncomment(source)
    cases = list(CASE.finditer(code))
    if len(cases) != 1:
        raise ValueError("legacy case missing or duplicated")
    start = cases[0].end()
    # Brace counting ignores strings; the initializer braces remain real code.
    masked = TOKENS.sub(lambda m: " " * len(m[0]), code)
    depth, end = 1, start
    while end < len(masked) and depth:
        depth += (masked[end] == "{") - (masked[end] == "}")
        end += 1
    if depth:
        raise ValueError("legacy case has no closing brace")
    body = code[start:end-1]
    arrays = list(LITERAL.finditer(code))
    if len(arrays) != 1 or not (start <= arrays[0].start() < arrays[0].end() < end):
        raise ValueError("legacy literal missing, duplicated or outside its case")
    values = re.findall(r'0x([0-9a-fA-F]{2})', arrays[0][1])
    residue = re.sub(r'0x[0-9a-fA-F]{2}|[\s,]', '', arrays[0][1])
    if residue or bytes.fromhex(''.join(values)) != LEGACY:
        raise ValueError("legacy literal differs from the captured 40 bytes")
    use = (r'remote_body_decode\([^;]*std::span<const\s+uint8_t>\(\s*kRefLegacySealed\s*,\s*'
           r'sizeof\s+kRefLegacySealed\s*\)')
    if len(re.findall(use, body)) != 1:
        raise ValueError("legacy literal is not the v2 decoder input")
    if not re.search(r'for\s*\(uint8_t\s+outer\s*:\s*\{uint8_t\(DATA_TYPE_REMOTE_CMD\),\s*'
                     r'uint8_t\(DATA_TYPE_REMOTE_RESP\)\}', body):
        raise ValueError("legacy rejection loop must exercise both outer types")
    if 'st != RemoteStatus::ok' not in body or 'sentinel_intact(got)' not in body:
        raise ValueError("legacy rejection assertions are missing")
    return cases[0].start(), end, arrays[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compare', type=Path, required=True)
    parser.add_argument('--freeze-check', action='store_true')
    parser.add_argument('--selftest', action='store_true')
    args = parser.parse_args()
    assert len(LEGACY) == 40 and hashlib.sha256(LEGACY).hexdigest() == LEGACY_SHA256
    path = Path(__file__).with_name('2026-09-13-radmin-slice7b3-0-reference.py')
    spec = importlib.util.spec_from_file_location('slice7b3_reference', path)
    old = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(old)
    saved = sys.argv
    try:
        sys.argv = [str(path), '--compare', str(args.compare)]
        if args.freeze_check:
            sys.argv.append('--freeze-check')
        if args.selftest:
            sys.argv.append('--selftest')
        old.main()
    finally:
        sys.argv = saved
    source = args.compare.read_text(encoding='utf-8')
    start, end, literal = check_legacy(source)
    print('Legacy capture: 1/1 (40 bytes), inside the live two-outer rejection case')
    if args.selftest:
        byte = re.search(r'0x[0-9a-fA-F]{2}', literal[1])
        at = literal.start(1) + byte.start()
        controls = {
            'case removed': source[:start] + source[end:],
            'literal removed': source[:literal.start()] + source[literal.end():],
            'one byte changed': source[:at] + '0xdf' + source[at+4:],
            'literal outside case': source[:literal.start()] + source[literal.end():] + '\n' + literal[0],
            'decoder input disconnected': source.replace('(kRefLegacySealed, sizeof kRefLegacySealed)', '(nullptr, 0)', 1),
        }
        for name, candidate in controls.items():
            try:
                check_legacy(candidate)
            except ValueError as error:
                print(f'LEGACY CONTROL RED ({name}): {error}')
            else:
                raise ValueError('legacy control GREEN: ' + name)
    print('PASS: 94 unchanged independent arrays + 1 frozen legacy frame')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError) as error:
        sys.exit(str(error))
