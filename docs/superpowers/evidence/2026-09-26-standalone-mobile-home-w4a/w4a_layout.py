#!/usr/bin/env python3
# W4a supplemental layout measurement (brief §4 / W4R-2) — the pre-check's layout-measure.py BASE recipe, nothing else.
# Reads the CURRENT model and invite headers, extracts the CURRENT `OutcomeView` declaration exactly once from
# src/firmware_ui.cpp, writes a temporary TU OUTSIDE the checkout, and measures sizeof/alignof with the stock ABI
# module's `measure` (each target's real flags/toolchain/nm). No counterfactual capacity edit, no mirror declaration,
# no stock-probe edit. usage: w4a_layout.py <scratch-dir> <out.json>   (run from the MeshRoute root)
import hashlib, importlib.util, json, re, sys
from pathlib import Path

root = Path.cwd()
scratch = Path(sys.argv[1]).resolve(); out = Path(sys.argv[2]).resolve()
scratch.mkdir(parents=True, exist_ok=True)
assert not str(scratch).startswith(str(root)), "the temporary TU must live outside the checkout"
spec = importlib.util.spec_from_file_location('w4a_abi', root / 'tools/probe_board_abi.py')
abi = importlib.util.module_from_spec(spec); sys.modules[spec.name] = abi; spec.loader.exec_module(abi)

inputs = {p: root / p for p in ('src/firmware_ui_model.h', 'src/firmware_ui_invite.h', 'src/firmware_ui.cpp',
                                'tools/probe_board_abi.py')}
model = inputs['src/firmware_ui_model.h'].read_text()
inv = inputs['src/firmware_ui_invite.h'].read_text()
cpp = inputs['src/firmware_ui.cpp'].read_text()
found = re.findall(r'struct OutcomeView \{.*?\n\};', cpp, re.S)
assert len(found) == 1, f"OutcomeView must be declared exactly once in firmware_ui.cpp, found {len(found)}"
outcome = found[0]
types = ['mrui::UiState', 'mrui::UiSnapshot', 'mrui::UiModel', 'mrui::TeamRow', 'mrui::InviteMember',
         'mrui::InviteIdRows', 'OutcomeView']
d = scratch / 'layout-base'; d.mkdir(exist_ok=True)
(d / 'firmware_ui_model.h').write_text(model); (d / 'firmware_ui_invite.h').write_text(inv)
tu = ('#include "' + str(d / 'firmware_ui_model.h') + '"\n' + outcome + '\nextern "C" {\n' +
      ''.join('char mr_abi_size__' + abi.slug(t) + '[sizeof(' + t + ')];\nchar mr_abi_align__' + abi.slug(t) +
              '[alignof(' + t + ')];\n' for t in types) + '}\n')
(d / 'query.cpp').write_text(tu)
result = {'inputs': {p: hashlib.sha256(f.read_bytes()).hexdigest() for p, f in inputs.items()},
          'generated_tu': str(d / 'query.cpp'),
          'generated_tu_sha256': hashlib.sha256(tu.encode()).hexdigest(),
          'outcome_view_decl_sha256': hashlib.sha256(outcome.encode()).hexdigest(),
          'targets': {}}
for target in abi.TARGETS:
    sizes = abi.measure(target, tu)
    result['targets'][target] = {t: {'size': sizes.get('mr_abi_size__' + abi.slug(t)),
                                     'align': sizes.get('mr_abi_align__' + abi.slug(t))} for t in types}
out.write_text(json.dumps(result, indent=2) + '\n')
for target, v in result['targets'].items():
    print(target, ' '.join(f"{t.split('::')[-1]}={v[t]['size']}/{v[t]['align']}" for t in types), flush=True)
