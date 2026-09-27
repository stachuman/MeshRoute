"""Counterfactual STRUCT-ONLY pricing, not implementation or a linked RAM claim. Scratch headers outside both repos."""
from pathlib import Path
import re,sys,hashlib,importlib.util,json,argparse,tempfile
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output', required=True, help='JSON output path OUTSIDE both repositories');args=parser.parse_args()
root=Path.cwd();output=Path(args.output).resolve();sim=Path('/home/staszek/lora-universal-simulator').resolve()
assert not output.is_relative_to(root.resolve()) and not output.is_relative_to(sim), 'measurement output must be outside both repositories'
scratch=Path(tempfile.mkdtemp(prefix='meshroute-w4b-layout-'))
spec=importlib.util.spec_from_file_location('qa_abi',root/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(spec);sys.modules[spec.name]=abi;spec.loader.exec_module(abi)
model=(root/'src/firmware_ui_model.h').read_text();chrome=(root/'src/firmware_ui_chrome.h').read_text();cpp=(root/'src/firmware_ui.cpp').read_text()
extra='\n'.join(re.search(r'struct '+n+r' \{.*?\n\};',cpp,re.S)[0] for n in ('OutcomeView','SettingsView'))
base_types=['mrui::UiState','mrui::UiSnapshot','mrui::UiModel','mrui::UiChrome','mrui::FrameGate','mrui::TeamRow','mrui::InviteMember','mrui::InviteIdRows','OutcomeView','SettingsView']
new_types='''enum class QaHomeItem : uint8_t { none, inbox, send, team, invite, my_device, menu, join, create, key_help };
enum class QaHomeView : uint8_t { list, my_device, key_help, setup_block };
enum class QaSetupOrigin : uint8_t { none, home, settings };
struct QaHomeCapture { QaHomeItem items[6]{}; uint8_t count=0; QaHomeItem selected=QaHomeItem::none; bool changed=false; };
'''
result=dict(scope=__doc__,inputs={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ('src/firmware_ui_model.h','src/firmware_ui_chrome.h','src/firmware_ui.cpp','tools/probe_board_abi.py')},variants={})
for name in ('baseline','reuse_focus_append_name','reuse_focus_padding_len','new_focus_and_rail','compact_profile'):
 m=model;c=chrome;types=base_types[:]
 if name!='baseline':
  defs=new_types
  if name=='compact_profile':
   defs=defs.replace('QaHomeItem items[6]{}; uint8_t count=0;', 'uint8_t profile=0;')
  m=m.replace('struct UiState {',defs+'\nstruct UiState {',1)
  anchor='    InviteGrantResult grant{};';assert m.count(anchor)==1
  members='\n    QaHomeCapture qa_home{};\n    QaHomeView qa_home_view=QaHomeView::list;'
  if name=='new_focus_and_rail':members+='\n    bool qa_menu_mode=false;\n    uint8_t qa_rail_index=0;'
  m=m.replace(anchor,anchor+members,1)
  anchor='    GrantReturn _grant_return{};';assert m.count(anchor)==1;m=m.replace(anchor,anchor+'\n    QaHomeItem qa_home_return=QaHomeItem::none;\n    QaSetupOrigin qa_setup_origin=QaSetupOrigin::none;',1)
  found=re.search(r'struct UiSnapshot \{.*?\n\};',m,re.S);assert found
  insert='    char qa_own_name[32]{};\n'+('    uint8_t qa_own_name_len=0;\n' if name!='reuse_focus_padding_len' else '')
  pos=found.end()-2;m=m[:pos]+insert+m[pos:]
  if name=='reuse_focus_padding_len':
   anchor='    uint8_t  nearby_n = 0;';assert m.count(anchor)==1;m=m.replace(anchor,anchor+'\n    uint8_t qa_own_name_len=0;',1)
  found=re.search(r'struct UiChrome \{.*?\n\};',c,re.S);assert found;pos=found.end()-2;c=c[:pos]+'    bool qa_menu_mode=false;\n'+c[pos:]
  types+=['mrui::QaHomeCapture']
 d=scratch/name;d.mkdir(exist_ok=True);(d/'firmware_ui_model.h').write_text(m);(d/'firmware_ui_chrome.h').write_text(c)
 tu='#include "'+str(d/'firmware_ui_chrome.h')+'"\n'+extra+'\nextern "C" {\n'+''.join('char mr_abi_size__'+abi.slug(t)+'[sizeof('+t+')];\nchar mr_abi_align__'+abi.slug(t)+'[alignof('+t+')];\n' for t in types)+'}\n'
 (d/'query.cpp').write_text(tu);item=dict(model_sha256=hashlib.sha256(m.encode()).hexdigest(),chrome_sha256=hashlib.sha256(c.encode()).hexdigest(),tu=str(d/'query.cpp'),targets={})
 for target in abi.TARGETS:
  vals=abi.measure(target,tu);item['targets'][target]={t:dict(size=vals['mr_abi_size__'+abi.slug(t)],align=vals['mr_abi_align__'+abi.slug(t)]) for t in types}
 result['variants'][name]=item;print(name,{target:{t.split('::')[-1]:v['size'] for t,v in typeset.items()} for target,typeset in item['targets'].items()},flush=True)
output.write_text(json.dumps(result,indent=2)+'\n')
