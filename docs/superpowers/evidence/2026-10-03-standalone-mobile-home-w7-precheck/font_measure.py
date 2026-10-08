from pathlib import Path
import json,subprocess,hashlib
R=Path('/home/staszek/MeshRoute');O=Path(__file__).resolve().parent;D=Path(json.loads((O/'scratch.json').read_text())['path']);font=R/'.pio/libdeps/heltec_mobile/U8g2/src/clib'
c=R/'docs/superpowers/evidence/2026-09-26-standalone-mobile-home-w4a-precheck/font-proof.c';out=D/'font-proof'
a=['cc','-std=c11','-O2','-ffunction-sections','-fdata-sections','-I'+str(font),str(c),str(font/'u8g2_font.c'),str(font/'u8g2_fonts.c'),'-Wl,--gc-sections','-o',str(out)]
p=subprocess.run(a,capture_output=True);assert p.returncode==0,p.stderr
p=subprocess.run([str(out)],capture_output=True);assert p.returncode==0
(O/'font-glyphs.txt').write_bytes(p.stdout)
vals={int(l.split()[0],16):dict(x.split('=') for x in l.split()[1:]) for l in p.stdout.decode().splitlines()[:256]}
rep='ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,?!-';assert len(rep)==42 and len(set(rep))==42
assert all(vals[ord(x)]=={'present':'1','advance':'6'} for x in rep+'_')
strings=['NAME           4/32','>ABCDEF GHIJKL',' MNOPQR STUVWX',' A B C>D E F BACK','>DEL LEFT RIGHT',' DONE DISCARD BACK','ADD SPACE','BACK TO GROUPS','SAVE NAME?',' SAVE >EDIT',' CHANGE NAME >BACK','NO NAME SET',' SET NAME','>SKIP','NAME SAVED','NAME NOT SAVED','NV WRITE FAILED']
assert all(len(x)<=19 for x in strings)
result=dict(command=a,repertoire=rep,repertoire_bytes=len(rep),groups=[rep[i:i+6] for i in range(0,42,6)],glyphs={f'{ord(x):02x}':vals[ord(x)] for x in rep+'_'},layout_strings={x:len(x) for x in strings},font_inputs={str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [c,font/'u8g2_font.c',font/'u8g2_fonts.c',font/'u8g2.h',R/'variants/heltec_common/board_ui.cpp']})
(O/'font-measure.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: 42 repertoire bytes + underscore, all glyphs present, 6 px advance; strings <=19')
