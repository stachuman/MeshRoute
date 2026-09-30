"""Labelled synthetic stdout fault in the real AST-extracted run_suite; no harness import/build."""
import ast
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from types import SimpleNamespace
import os

source = Path('tools/probe_ui_model_mutations.py').read_text()
fn = next(n for n in ast.parse(source).body if isinstance(n, ast.FunctionDef) and n.name == 'run_suite')
body = ast.get_source_segment(source, fn)
real_run = subprocess.run

def synthetic_run(command, **kwargs):
    if command[0] == 'pio':
        return SimpleNamespace(returncode=0, stdout='', stderr='')
    return real_run(command, **kwargs)

with tempfile.TemporaryDirectory(prefix='mr-w6-decode-qa-') as scratch:
    program = Path(scratch) / '.pio/build/native/program'
    program.parent.mkdir(parents=True)
    program.write_text('#!/usr/bin/env python3\nimport sys\nsys.stdout.buffer.write(b"synthetic label: \\xbb\\n")\n')
    program.chmod(0o755)
    env = dict(ROOT=scratch, subprocess=SimpleNamespace(run=synthetic_run), os=os, re=re)
    exec(compile(body, '<real run_suite, AST extraction>', 'exec'), env)
    try:
        env['run_suite']()
    except UnicodeDecodeError as error:
        print(json.dumps(dict(scope='synthetic stdout, actual run_suite, successful build stub', function_sha256=hashlib.sha256(body.encode()).hexdigest(), exception=type(error).__name__, detail=str(error)), indent=2))
    else:
        raise SystemExit('reproduction absent: inspect the changed harness')
