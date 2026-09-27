from pathlib import Path
import json,re,importlib.util,sys
root=Path.cwd(); raw=root/'artifacts/2026-09-26-standalone-mobile-home-w4a-precheck'; scratch=Path(json.loads((raw/'scratch.json').read_text())['path'])
spec=importlib.util.spec_from_file_location('w4a_abi',root/'tools/probe_board_abi.py'); abi=importlib.util.module_from_spec(spec);sys.modules[spec.name]=abi;spec.loader.exec_module(abi)
model=(root/'src/firmware_ui_model.h').read_text();inv=(root/'src/firmware_ui_invite.h').read_text();cpp=(root/'src/firmware_ui.cpp').read_text();outcome=re.search(r'struct OutcomeView \{.*?\n\};',cpp,re.S)[0]
types=['mrui::UiState','mrui::UiSnapshot','mrui::UiModel','mrui::TeamRow','mrui::InviteMember','mrui::InviteIdRows','OutcomeView']
answer={}
for case in ['base','all-label-caps-19','invite-name-only-19','reply-only-19']:
 m=model;i=inv;o=outcome
 if case=='all-label-caps-19':m=m.replace('kLabelCap     = 14','kLabelCap     = 19');i=i.replace('kInviteNameCap = 15','kInviteNameCap = 20')
 if case=='invite-name-only-19':
  i=i.replace('kInviteNameCap = 15','kInviteNameCap = 20')
  m=re.sub(r'static_assert\(std::size_t\(kInviteNameCap\).*?;', '// Counterfactual measurement ONLY: independent capacity, equality assert omitted.',m,count=1,flags=re.S)
 if case=='reply-only-19':m=m.replace('_reply_who[kLabelCap + 1]','_reply_who[20]');o=o.replace('who[mrui::kLabelCap + 1]','who[20]')
 d=scratch/('layout-'+case);d.mkdir(exist_ok=True);(d/'firmware_ui_model.h').write_text(m);(d/'firmware_ui_invite.h').write_text(i)
 tu='#include "'+str(d/'firmware_ui_model.h')+'"\n'+o+'\nextern "C" {\n'+''.join('char mr_abi_size__'+abi.slug(t)+'[sizeof('+t+')];\nchar mr_abi_align__'+abi.slug(t)+'[alignof('+t+')];\n' for t in types)+'}\n'
 (d/'query.cpp').write_text(tu);answer[case]={}
 for target in abi.TARGETS:
  sizes=abi.measure(target,tu);answer[case][target]=sizes
 (raw/'layout-measurements.json').write_text(json.dumps(answer,indent=2)+'\n')
 print(case,json.dumps(answer[case]),flush=True)
