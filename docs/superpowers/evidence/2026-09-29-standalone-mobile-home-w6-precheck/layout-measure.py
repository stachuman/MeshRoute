"""Proposed STRUCT-ONLY W6 allocation; scratch copies, no implementation or linked-RAM claim."""
from pathlib import Path
import tempfile,shutil,subprocess,importlib.util,sys,re,json,hashlib
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;D=Path(tempfile.mkdtemp(prefix='w6-layout-'))
spec=importlib.util.spec_from_file_location('w6abi',R/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(spec);sys.modules[spec.name]=abi;spec.loader.exec_module(abi)
reply=json.loads((E/'reply-measure.json').read_text());catroot=Path(reply['scratch'])/'src'
types=['mrnv::UiPresetSlot','mrnv::UiPresetBlob','mrfw::PresetCatalog','mrui::ComposeSlot','mrui::ComposeList','mrui::SendReq','mrui::UiState','mrui::UiSnapshot','mrui::UiModel','mrui::UiChrome','mrnv::Blob','mrnv::IdBlob','mrnv::PeerBlob','mrnv::JoinBlob','mrnv::TeamKeyBlob','mrnv::AdminIdBlob','mrnv::AclBlob','mrnv::MgmtKeyBlob','mrnv::TargetBlob','mrfault::FaultLog']
result=dict(scope=__doc__,scratch=str(D),variants={})
for variant in ['base','catalog_only','shared_page','separate_page','separate_body_page']:
 d=D/variant;shutil.copytree(R/'src' if variant=='base' else catroot,d)
 m=(d/'firmware_ui_model.h').read_text()
 if variant not in ('base','catalog_only'):
  # Full, valid hash domain: known bit occupies existing SendReq byte padding, before generation.
  a='    uint8_t  slot       = 0;';assert m.count(a)==1;m=m.replace(a,'    bool qa_peer_known = false;\n'+a) # keeps initializer mapping? STRUCT-ONLY: move below generation instead for positional initializers
  m=m.replace('    bool qa_peer_known = false;\n','')
  a='    uint32_t generation = 0;      //';assert m.count(a)==1;m=m.replace(a,'    uint32_t generation = 0;      //',1)
  # Insert the known bit AFTER the old four members. This preserves current aggregate initializers and measures honest padding.
  found=re.search(r'struct SendReq \{.*?\n\};',m,re.S);assert found
  block=found[0].replace('\n};','\n    uint32_t qa_team_id=0, qa_peer_hash=0;\n    bool qa_peer_known=false;\n};');m=m[:found.start()]+block+m[found.end():]
  # Compact alternative: known flag occupies the old byte hole without disturbing initializer meaning via explicitly changed initializers below.
  block=re.search(r'struct SendReq \{.*?\n\};',m,re.S)[0];block2=block.replace('    bool qa_peer_known=false;\n','').replace('    uint32_t generation =','    bool qa_peer_known=false;\n    uint32_t generation =')
  m=m.replace(block,block2)
  # The model's two existing four-field constructions adapt only to permit measuring the proposed type.
  m=m.replace('SendReq{SendKind::emergency, 0, mrfw::kPresetEmergency, 0}', 'SendReq{SendKind::emergency, 0, mrfw::kPresetEmergency, false, 0}')
  m=m.replace('_req = {k, peer, slot, gen};','_req = {k, peer, slot, false, gen};')
  # Reuse the existing 242-byte modal body, its length/page/page-count/cadence; phrase and inbox detail cannot overlap.
  # Shared-page union leaves original detail_line type and every fixed-two-row caller untouched.
  if variant=='shared_page':
   a='    char        detail_line[kDetailBodyRows][kDetailCols + 1] = {};';assert m.count(a)==1
   m=m.replace(a,'    union { char detail_line[kDetailBodyRows][kDetailCols + 1] = {}; char qa_review_line[3][kDetailCols + 1]; };')
  found=re.search(r'struct UiState \{.*?\n\};',m,re.S);assert found
  extra='    uint8_t qa_review_phase=0;\n    bool qa_review_send=false, qa_review_loc=false;\n    char qa_review_header[20]{};\n'
  if variant!='shared_page': extra+='    char qa_review_line[3][kDetailCols+1]{};\n'
  # Frozen row content and action/header in UiState; independent selection binding in model to survive result/request sequencing.
  pos=found.end()-2;m=m[:pos]+extra+m[pos:]
  a='    SendReq  _req{};';assert m.count(a)==1;m=m.replace(a,a+'\n    SendReq qa_review_binding{};')
  if variant=='separate_body_page':
   m=m.replace('    SendReq qa_review_binding{};','    SendReq qa_review_binding{};\n    char qa_review_body[164]{};\n    uint8_t qa_review_len=0, qa_review_page=0, qa_review_pages=1;\n    uint32_t qa_review_page_at_ms=0;')
  (d/'firmware_ui_model.h').write_text(m)
 tu='#include "'+str(d/'firmware_ui_chrome.h')+'"\nextern "C" {\n'+''.join(f'char mr_abi_size__{abi.slug(t)}[sizeof({t})];\nchar mr_abi_align__{abi.slug(t)}[alignof({t})];\n' for t in types)+'}\n'
 item={'targets':{},'model_sha256':hashlib.sha256(m.encode()).hexdigest()}
 for target in abi.TARGETS:
  vals=abi.measure(target,tu);item['targets'][target]={t:{'size':vals['mr_abi_size__'+abi.slug(t)],'align':vals['mr_abi_align__'+abi.slug(t)]} for t in types}
 result['variants'][variant]=item
 print(variant,{t:{k.split('::')[-1]:v['size'] for k,v in x.items() if k in types[:9]} for t,x in item['targets'].items()},flush=True)
 (E/'layout-measure.json').write_text(json.dumps(result,indent=2)+'\n')
