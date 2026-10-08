"""STRUCT-ONLY W7+W8 object pricing; no implementation or linked RAM."""
from pathlib import Path
import tempfile,shutil,importlib.util,sys,json,hashlib
R=Path('/home/staszek/MeshRoute');O=Path(__file__).resolve().parent;D=Path(tempfile.mkdtemp(prefix='w8-layout-'))
spec=importlib.util.spec_from_file_location('w8abi',R/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(spec);sys.modules[spec.name]=abi;spec.loader.exec_module(abi)
ts=['mrui::UiState','mrui::UiSnapshot','mrui::UiModel','mrui::UiChrome','mrui::SendReq','mrui::SendLive','mrui::SendTracker']
res={'scope':__doc__,'scratch':str(D),'variants':{}}
for variant in ['base','d16_sendreq_state']:
 d=D/variant;shutil.copytree(R/'src',d);m=(d/'firmware_ui_model.h').read_text();types=ts.copy()
 if variant!='base':
  a='enum class HomeItem : uint8_t';assert m.count(a)==1
  decl='''struct QaEditorWindow { uint8_t phase=0, group=0, item=0, used=0, cap=0, cursor_row=0, cursor_col=0, note=0, result=0; bool primary_selected=false; };
enum class QaNameOrigin : uint8_t { none, my_device, setup_join, setup_create };
struct QaDraft { char bytes[163]{}; uint8_t len=0, cursor=0, caller=0, cap=0; uint32_t draft_id=0; bool locked=false; };
enum class QaWrittenState : uint8_t { none, queued, known_refused, accepted_open, accepted_final };
struct QaWrittenOutcome { QaWrittenState state=QaWrittenState::none; uint8_t reason=0, refusal=0, code=0; };
'''
  m=m.replace(a,decl+a)
  a='        char    review_line[kReviewBodyRows][kDetailCols + 1];';assert m.count(a)==1;m=m.replace(a,a+'\n        char qa_editor_line[2][kDetailCols + 1];')
  a='    char        review_header[kReviewHeaderCap] = {};';assert m.count(a)==1;m=m.replace(a,a+'\n    QaEditorWindow qa_editor{};')
  a='    SetupOrigin _setup_origin = SetupOrigin::none;';assert m.count(a)==1;m=m.replace(a,a+'\n    QaDraft qa_draft{};\n    QaNameOrigin qa_name_origin=QaNameOrigin::none;'+('\n    QaWrittenOutcome qa_written_outcome{};' if variant=='d16_sendreq_state' else ''))
  types+=['mrui::QaDraft','mrui::QaEditorWindow','mrui::QaNameOrigin','mrui::QaWrittenState','mrui::QaWrittenOutcome']
 if variant.startswith('d16_sendreq'):
  a='    uint32_t peer_hash  = 0;';assert m.count(a)==1;m=m.replace(a,'    uint32_t draft_id=0;\n'+a)
 (d/'firmware_ui_model.h').write_text(m)
 sh=(d/'firmware_ui_send.h').read_text()
 anchor='    uint32_t peer_hash  = 0;      // its hash, meaningful only when `peer_found`'
 assert sh.count(anchor)==1
 sh=sh.replace(anchor,'    bool team_id_exists=false;\n'+anchor)
 (d/'firmware_ui_send.h').write_text(sh)

 tu='#include "'+str(d/'firmware_ui_send.h')+'"\n#include "'+str(d/'firmware_ui_chrome.h')+'"\nextern "C" {\n'+''.join(f'char mr_abi_size__{abi.slug(t)}[sizeof({t})];\nchar mr_abi_align__{abi.slug(t)}[alignof({t})];\n' for t in types)+'}\n'
 item={'model_sha256':hashlib.sha256(m.encode()).hexdigest(),'targets':{}}
 for target in abi.TARGETS:
  vals=abi.measure(target,tu);item['targets'][target]={t:{'size':vals['mr_abi_size__'+abi.slug(t)],'align':vals['mr_abi_align__'+abi.slug(t)]} for t in types}
 res['variants'][variant]=item;(O/'layout-measure.json').write_text(json.dumps(res,indent=2)+'\n');print(variant,{k:{t.split('::')[-1]:v['size'] for t,v in vs.items()} for k,vs in item['targets'].items()},flush=True)
