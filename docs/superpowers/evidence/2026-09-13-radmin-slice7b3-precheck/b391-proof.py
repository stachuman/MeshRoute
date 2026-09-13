from pathlib import Path
import subprocess,hashlib,json,difflib
q=Path(__file__).resolve().parent;r=q/'snapshot';p=r/'tools/probe_ui_model_mutations.py';orig=p.read_text()
old="    // to exactly what was already armed.\n    radmin_expiry_arm();"
new="    radmin_expiry_arm();\n}\n#endif\n\n#if MR_FEAT_RADMIN_CLIENT"
assert (r/'lib/core/node_mac_rx.cpp').read_text().count(new)==1
oldpair=repr(old)+',\n  '+repr(old.replace('radmin_expiry_arm();',';'))
newpair=repr(new)+',\n  '+repr(new.replace('radmin_expiry_arm();',';'))
assert orig.count(oldpair)==1
proposed=orig.replace(oldpair,newpair)
(q/'b391-proposed-pattern.patch').write_text(''.join(difflib.unified_diff(orig.splitlines(True),proposed.splitlines(True),fromfile='a/tools/probe_ui_model_mutations.py',tofile='b/tools/probe_ui_model_mutations.py')))
try:
 p.write_text(proposed)
 with (q/'logs/b391-x09-proposed.log').open('w') as f:
  rc=subprocess.run(['python3','tools/probe_ui_model_mutations.py','--target=radmin5rx','X09'],cwd=r,stdout=f,stderr=subprocess.STDOUT).returncode
finally:p.write_text(orig)
(q/'b391-result.json').write_text(json.dumps({'original_rc':1,'original_match':0,'proposed_rc':rc,'proposed_match':1,'proposal_applied_to_shared_tree':False,'private_harness_restored':p.read_text()==orig,'source_sha256':hashlib.sha256((r/'lib/core/node_mac_rx.cpp').read_bytes()).hexdigest()},indent=2)+'\n')
assert rc==0
