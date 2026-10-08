"""STRUCT-ONLY candidate pricing, not implementation or linked RAM. Uses stock ABI toolchains."""
from pathlib import Path
import tempfile,shutil,importlib.util,sys,re,json,hashlib
R=Path('/home/staszek/MeshRoute');O=Path(__file__).resolve().parent;D=Path(tempfile.mkdtemp(prefix='w7-layout-'))
spec=importlib.util.spec_from_file_location('w7abi',R/'tools/probe_board_abi.py');abi=importlib.util.module_from_spec(spec);sys.modules[spec.name]=abi;spec.loader.exec_module(abi)
types=['mrui::UiState','mrui::UiSnapshot','mrui::UiModel','mrui::UiChrome','mrui::HomeCapture','mrui::HomeView','mrui::SetupOrigin','mrui::ReviewPhase','mrui::SendReq','mrui::InputFsm']
result=dict(scope=__doc__,scratch=str(D),variants={})
for variant,capacity in [('base',0),('name32',32),('shared163',163)]:
 d=D/variant;shutil.copytree(R/'src',d);m=(d/'firmware_ui_model.h').read_text()
 if capacity:
  before='enum class HomeItem : uint8_t';assert m.count(before)==1
  decl='''// QA STRUCT-ONLY candidate: no function, no executable path.
struct QaEditorWindow {
 uint8_t phase=0, group=0, item=0, used=0, cap=0, cursor_row=0, cursor_col=0, note=0, result=0;
 bool primary_selected=false;
};
enum class QaNameOrigin : uint8_t { none, my_device, setup_join, setup_create };
struct QaDraft { char bytes['''+str(capacity)+''']{}; uint8_t len=0, cursor=0, caller=0, cap=0;
'''+(' uint32_t draft_id=0; bool locked=false;\n' if capacity==163 else '')+'};\n'
  m=m.replace(before,decl+before)
  a='        char    review_line[kReviewBodyRows][kDetailCols + 1];';assert m.count(a)==1;m=m.replace(a,a+'\n        char qa_editor_line[2][kDetailCols + 1];')
  a='    char        review_header[kReviewHeaderCap] = {};';assert m.count(a)==1;m=m.replace(a,a+'\n    QaEditorWindow qa_editor{};')
  a='    SetupOrigin _setup_origin = SetupOrigin::none;';assert m.count(a)==1;m=m.replace(a,a+'\n    QaDraft qa_draft{};\n    QaNameOrigin qa_name_origin = QaNameOrigin::none;')
 (d/'firmware_ui_model.h').write_text(m)
 ts=types+(['mrui::QaDraft','mrui::QaEditorWindow','mrui::QaNameOrigin'] if capacity else [])
 tu='#include "'+str(d/'firmware_ui_chrome.h')+'"\nextern "C" {\n'+''.join(f'char mr_abi_size__{abi.slug(t)}[sizeof({t})];\nchar mr_abi_align__{abi.slug(t)}[alignof({t})];\n' for t in ts)+'}\n'
 item={'targets':{},'model_sha256':hashlib.sha256(m.encode()).hexdigest()}
 for target in abi.TARGETS:
  vals=abi.measure(target,tu);item['targets'][target]={t:{'size':vals['mr_abi_size__'+abi.slug(t)],'align':vals['mr_abi_align__'+abi.slug(t)]} for t in ts}
 result['variants'][variant]=item
 (O/'layout-measure.json').write_text(json.dumps(result,indent=2)+'\n');print(variant,{t:{k.split('::')[-1]:v['size'] for k,v in x.items()} for t,x in item['targets'].items()},flush=True)
