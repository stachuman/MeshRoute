import importlib.util
import json
import pathlib
import re
import subprocess
import sys

import argparse
parser = argparse.ArgumentParser(description='QA candidate layout only; no production edits or board links.')
parser.add_argument('--root', type=pathlib.Path, required=True)
parser.add_argument('--out', type=pathlib.Path, required=True)
args = parser.parse_args()
root = args.root.resolve()
gate = args.out.resolve()
gate.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('mr_qa_abi', root / 'tools/probe_board_abi.py')
abi = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = abi
spec.loader.exec_module(abi)
header = (root / 'lib/core/remote_session.h').read_text()
current = re.search(r'struct RemoteSessionState \{.*?\n\};', header, re.S).group(0)
capture = '''
namespace meshroute {
struct CandidateOpenCapture {
    uint8_t bytes[kRadminChunkSlots * kRadminChunkBytes];
    uint16_t len;
    uint16_t next_offset;
    uint16_t frame_cap;
    uint8_t phase;
    uint8_t truncated;
    uint8_t terminal;
    uint8_t next_seq;
};
struct CandidateOpenState {
    CandidateOpenCapture open[kRadminOpenSlots];
    uint16_t inbound_refusal;
    uint16_t open_rate_refusal;
};
'''
candidate = current.replace('struct RemoteSessionState {', 'struct CandidateRemoteSessionState {')
candidate = candidate[:-3] + '    CandidateOpenState slice7b2;\n};\n}\n'
fixture = capture + candidate
entries = (
    ('meshroute::RemoteSessionState', 'MR_FEAT_RADMIN_ACCEPT'),
    ('meshroute::OpenStagingSlot', 'MR_FEAT_RADMIN_ACCEPT'),
    ('meshroute::CandidateOpenCapture', 'MR_FEAT_RADMIN_ACCEPT'),
    ('meshroute::CandidateOpenState', 'MR_FEAT_RADMIN_ACCEPT'),
    ('meshroute::CandidateRemoteSessionState', 'MR_FEAT_RADMIN_ACCEPT'),
    ('meshroute::Node', '1'),
)
tu = abi.generate_tu(entries, fixture_source=abi.FIXTURE_SOURCE+'\n'+fixture)
(gate / 'candidate-layout.cpp').write_text(tu)
results = {}
for target in ('native', 'gateway', 'heltec_mobile'):
    data = abi.idedata(target)
    (gate / ('candidate-'+target+'-idedata.json')).write_text(json.dumps(data,indent=2)+'\n')
    obj = gate / ('candidate-'+target+'.o')
    command = abi.compile_command(data, gate/'candidate-layout.cpp', obj)
    result = subprocess.run(command, capture_output=True, text=True)
    (gate / ('candidate-'+target+'-compile.log')).write_text(result.stdout+result.stderr)
    assert result.returncode==0, result.stderr
    sizes = abi.read_sizes(abi.binutil(data['cxx_path'],'nm'), obj)
    assert sizes['mr_abi_size__meshroute_RemoteSessionState']==3848
    # Adverse layout control: one extra byte per capture must visibly increase the aggregate.
    enlarged = tu.replace('bytes[kRadminChunkSlots * kRadminChunkBytes]', 'bytes[kRadminChunkSlots * kRadminChunkBytes + 1]')
    assert enlarged!=tu
    changed=abi.measure(target,enlarged)
    key='mr_abi_size__meshroute_CandidateRemoteSessionState'
    assert changed[key]>sizes[key]
    results[target]={'command':command,'sizes':sizes,'extra_byte_control_size':changed[key]}
    print(target, sizes[key], 'delta', sizes[key]-3848, 'extra-byte control',changed[key],flush=True)
(gate / 'candidate-measurements.json').write_text(json.dumps(results,indent=2)+'\n')
