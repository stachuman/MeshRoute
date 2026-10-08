"""V1 emitter-based conservative retention ledger; not a production implementation."""
from pathlib import Path
import re,json
R=Path('/home/staszek/MeshRoute');O=Path(__file__).resolve().parent
s=(R/'lib/core/command.h').read_text();enum=s[s.index('enum class SendFailReason'):];enum=enum[:enum.index('};')];enum=re.sub(r'//[^\n]*','',enum);names=[x.strip().split('=')[0].strip()for x in enum.split('{',1)[1].split(',')];assert len(names)==18
known={'no_pubkey','no_identity','too_large','bad_rng','joining','cap','min_interval','mobile_no_home','unsealable','no_location'}
why={
'none':'No stage/air-history claim. Includes a pack failure of an already owned flight in node_mac.cpp.',
'no_pubkey':'Seal/admission refusal before the new DM is enqueued; never infer ownership from reason alone.',
'no_identity':'Identity/seal admission before origination enqueue.',
'too_large':'Size/seal admission before origination enqueue.',
'bad_rng':'Origination seal refuses before enqueue; channel seal failure still has no attributable async handle.',
'no_route':'Also produced while draining retained/deferred carriers. Do not assume first origination attempt.',
'joining':'Managed leaf admission refuses before enqueue, usually with ctr zero (not an exactly matched DM outcome).',
'cap':'Channel self-origination budget blocks before counter mint/enqueue. It is send_blocked, not matched DM send_failed.',
'min_interval':'Self-origination burst gate blocks before enqueue. Match channel send_blocked by existing scope/window only.',
'no_cts':'ACK timeout resets the same flight for a new RTS; a later CTS giveup need not be its first attempt.',
'no_ack':'DATA ACK timeout follows a transmission attempt; no ACK does not prove no reception.',
'mobile_no_home':'Mobile admission/wrapper preparation refuses before origination enqueue.',
'gateway_unreachable':'gateway_doorstep_hold is also called from ack_timeout_fire after a DATA attempt.',
'e2e_ack_timeout':'Delivery not confirmed; a late send_e2e_acked may upgrade the open result.',
'queue_full':'Deferred/held carriers can retain an earlier attempt. The generic failure vocabulary does not prove first attempt.',
'reprovisioned':'Purge includes PendingTx, potentially awaiting ACK. The enum comment is not a proof of never-airing.',
'unsealable':'Type/location/seal admission before the new message is enqueued.',
'no_location':'Requested location admission refuses before origination enqueue; written no-l cannot originate it normally.'}
producers=[]
for p in sorted((R/'lib/core').glob('*.cpp')):
 lines=p.read_text().splitlines()
 for i,l in enumerate(lines):
  code=l.split('//',1)[0]
  for n in names:
   if 'SendFailReason::'+n in code:
    producers.append(dict(file=str(p.relative_to(R)),line=i+1,reason=n,context='\n'.join(f'{j+1}: {lines[j]}'for j in range(max(0,i-3),min(len(lines),i+4)))))
ledger=[dict(reason=n,class_= 'known_not_aired' if n in known else 'may_have_aired',basis=why[n],sites=[{k:x[k]for k in ('file','line')}for x in producers if x['reason']==n]) for n in names]
(O/'failure-classification.json').write_text(json.dumps(dict(policy='Only an attributable outcome may update a written request; unknown enumerators and unclassified producers are may-have-aired. No new channel send_failed matcher.',reasons=ledger,producer_contexts=producers),indent=2)+'\n')
print('18 enum values;',len(producers),'source occurrences;',len(known),'known admission reasons; unknown defaults conservatively.')
