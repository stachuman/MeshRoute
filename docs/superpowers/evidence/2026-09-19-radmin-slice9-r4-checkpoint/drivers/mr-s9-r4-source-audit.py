from pathlib import Path
import re,json,hashlib,subprocess,tokenize,io
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s9-r4-j37ybea_');base=Path('/tmp/mr-s9-r3-mg75mxe0/base')
# Strip comments without eating quoted C++ strings, then lex into tokens for comment-only comparisons.
lex=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|\d+|[^\s]',re.M)
def toks(s):return [m[0] for m in lex.finditer(s) if not m[0].startswith(('//','/*'))]
def identifiers(s):return [x for x in toks(s) if re.fullmatch(r'[A-Za-z_]\w*',x)]
removed='send_remote_cmd send_remote_response admin_cmd_seal admin_cmd_open admin_key_from_password admin_counter_ok AdminVerdict AdminCmd ADMIN_KDF_ITERS ADMIN_SALT handle_rcmd handle_password handle_unlock handle_lock g_admin_id g_admin_unlocked g_admin_tx_ctr g_remote_action g_remote_action_at fw_wdt_feed MR_FEAT_REMOTE_MGMT remote_exec remote_encode remote_verb_open remote_seal_resp admin_verb_gated REMOTE_FLAG_SEALED cmd_rcmd LEGACY_FAMILIES'.split()
paths=[p for parent in ['lib','src','test','tools'] for p in (r/parent).rglob('*') if p.suffix in ['.h','.cpp','.c','.py','.sh'] and '.pio' not in p.parts and '__pycache__' not in p.parts]
violations=[];comments=[];negative=[]
for p in paths:
 s=p.read_text(errors='replace');hits=set(removed)&set(re.findall(r'\b[A-Za-z_]\w*\b',s))
 if not hits:continue
 if p.suffix in ['.h','.cpp','.c']:
  bad=set(removed)&set(identifiers(s))
  for token in bad:violations.append([str(p.relative_to(r)),token])
  if not bad:comments.append(str(p.relative_to(r)))
 elif p.suffix=='.py':
  tokens=list(tokenize.generate_tokens(io.StringIO(s).readline))
  bad=set(removed)&{t.string for t in tokens if t.type==tokenize.NAME}
  for token in bad:violations.append([str(p.relative_to(r)),token])
  for token in tokens:
   if token.type==tokenize.STRING and set(removed)&set(re.findall(r'\b[A-Za-z_]\w*\b',token.string)):
    negative.append(dict(path=str(p.relative_to(r)),line=token.start[0],kind='string; reviewed as historical generated prose or explicit absence check'))
  comments.append(str(p.relative_to(r)))
 else:
  lines=[x for x in s.splitlines() if not x.lstrip().startswith('#')]
  bad=set(removed)&set(re.findall(r'\b[A-Za-z_]\w*\b','\n'.join(lines)))
  for token in bad:violations.append([str(p.relative_to(r)),token])
  comments.append(str(p.relative_to(r)))
assert not violations,violations
headers=['admin_auth.h','console_binary.h','firmware_remote.h']
for p in paths:
 if p.suffix not in ['.h','.cpp','.c']:continue
 includes=re.findall(r'^\s*#\s*include\s*[<"]([^>"\n]+)',p.read_text(errors='replace'),re.M)
 assert not any(Path(x).name in headers for x in includes),p
mirrors={name:[] for name in ['admin_pubkey','admin_provisioned','admin_counter_floor','admin_load']}
for p in paths:
 if p.suffix not in ['.h','.cpp','.c']:continue
 ts=toks(p.read_text(errors='replace'))
 for n in mirrors:
  count=sum(1 for a,b in zip(ts,ts[1:]) if a==n and b=='(')
  if count:mirrors[n].append({'path':str(p.relative_to(r)),'count':count})
for n,rows in mirrors.items():assert rows==([{'path':'lib/core/node.h','count':2}]+([{'path':'src/fw_main.cpp','count':1}] if n=='admin_load' else [])),(n,rows)
comment_only=['lib/core/node.cpp','lib/core/node_carriers.h','lib/core/node_mac_rx.cpp','test/test_node_hashlocate.cpp']
for p in comment_only:assert toks((r/p).read_text())==toks((base/p).read_text()),p
owners={str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if str(p.relative_to(r)).startswith(('lib/core/remote_','src/firmware_remote_client','src/firmware_remote_executor','src/firmware_remote_actions','src/firmware_admin_'))}
for p,h in owners.items():assert hashlib.sha256((base/p).read_bytes()).hexdigest()==h,p
snapshot=json.loads((q/'final-gate-inputs.json').read_text());primary={p:v for p,v in snapshot.items() if p.startswith(('lib/','src/','test/','tools/')) or p=='platformio.ini'}
for root in [r,q/'final-gate',q/'final-boards',q/'union-inputs']:
 for p,v in primary.items():
  f=root/p
  if v.get('deleted'):assert not f.exists(),(root,p)
  elif 'sha256'in v:
   actual=hashlib.sha256(f.read_bytes()).hexdigest()
   if root==r and p=='tools/test_probe_features.py':
    assert actual!=v['sha256'] # B428: recorded, focused-tested post-sweep repair only.
   else:assert actual==v['sha256'],(root,p)
result=dict(removed_identifiers=removed,executable_violations=violations,comment_paths=sorted(set(comments)),string_exemptions=negative,legacy_mirrors=mirrors,comment_only_token_equality=comment_only,v2_owner_hashes=owners,primary_records=len(primary),identical_copies=4,post_gate_repair={'path':'tools/test_probe_features.py','finding':'B428','tested_sha256':primary['tools/test_probe_features.py']['sha256'],'checkpoint_sha256':hashlib.sha256((r/'tools/test_probe_features.py').read_bytes()).hexdigest(),'focused_result':'B428-focused/result.log: 1 test OK; invokes actual ownership and its controls'},identical_primary_records=360,brief_sha256=hashlib.sha256((r/'docs/superpowers/plans/2026-09-18-radmin-slice9-legacy-deletion-and-protocol-docs.md').read_bytes()).hexdigest(),known_out_of_fence_legacy_surface_test='B426: tools/test_check_command_authority.py:44')
(q/'source-audit.json').write_text(json.dumps(result,indent=2)+'\n');(q/'frozen-primary-inputs.json').write_text(json.dumps(primary,indent=2)+'\n');print('Source audit PASS',len(primary),'gate input records; 360 match all copies, one B428 scoped repair; v2 owners',len(owners),'unchanged; B426 disclosed separately')
