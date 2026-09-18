#!/usr/bin/env python3
"""8b source-only fanout ownership; each counterexample must fail a named check."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'probe_console_sink'))
from structural import _neutral, _body

CALL = 'mrfw::remote_client_observe_push(g_node.remote_client(), pu, mrcon, &local_ble, mrble::connected(), mrfw::remote_client_bind_lookup, &g_node);'

def check(source):
    source = _neutral(source)
    loop = _body(source, 'while (g_node.next_push(pu))')
    fanout = _body(loop, 'if (mrble::connected())')
    after = loop[loop.index(fanout) + len(fanout):]
    return {
        'O1 sole observer is in push drain': source.count('mrfw::remote_client_observe_push(') == 1 and CALL in loop,
        'O2 observer follows completed generic BLE fanout': CALL in after,
        'O3 observer is CLIENT guarded with a call-scoped sink': '#if MR_FEAT_RADMIN_CLIENT' in after and
            after.index('#if MR_FEAT_RADMIN_CLIENT') < after.find(CALL) < after.index('#endif') and
            'LineSink local_ble(ble_sink);' in after,
        'O4 one whole-push call uses current connection and flushes': CALL in after and 'local_ble.flush();' in after,
    }

def main():
    source = Path(sys.argv[1]).read_text()
    rows = check(source)
    for name, ok in rows.items():
        print(f'observer structure {"PASS" if ok else "FAIL"}: {name}')
    if not all(rows.values()): return 1
    if '--no-neg' not in sys.argv:
        marker = '        if (mrble::connected()) {\n            // Sized for the TRUE worst case:'
        controls = {
            'missing lookup': source.replace(', mrfw::remote_client_bind_lookup, &g_node', ''),
            'wrong lookup context': source.replace(', mrfw::remote_client_bind_lookup, &g_node', ', mrfw::remote_client_bind_lookup, nullptr'),
            'missing observer': source.replace(CALL, ''),
            'before fanout': source.replace(CALL, '').replace(marker, '        ' + CALL + '\n' + marker),
            'lost CLIENT guard': source.replace('#if MR_FEAT_RADMIN_CLIENT\n        { LineSink', '#if 1\n        { LineSink'),
            'wrong push': source.replace(', pu, mrcon, &local_ble,', ', other_push, mrcon, &local_ble,'),
        }
        for name, mutated in controls.items():
            if mutated == source or all(check(mutated).values()):
                print(f'observer structure control FAIL: {name}');return 1
            print(f'observer structure control RED: {name}')
    return 0

if __name__ == '__main__': sys.exit(main())
