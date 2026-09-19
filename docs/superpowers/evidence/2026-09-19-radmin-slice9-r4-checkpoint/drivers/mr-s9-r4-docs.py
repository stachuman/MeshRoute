from pathlib import Path

def edit(name, old, new):
 p=Path(name);s=p.read_text();assert old in s,(name,old[:70]);p.write_text(s.replace(old,new))
edit('src/firmware_config.cpp','the seven call sites differ','the six call sites differ')
edit('src/firmware_config.cpp','THE SEVEN USER-INITIATED SITES','THE SIX USER-INITIATED SITES')
edit('src/firmware_config.cpp','The remaining four verbs assign none','The remaining three provisioning verbs assign none')
edit('src/firmware_config.cpp','one of the seven sites','one of the six sites')
edit('src/firmware_config.cpp','W12/W14-W19 pin the seven placements','W12/W14-W18 pin the six placements')
edit('platformio.ini','(static, KEEPS remote-mgmt) and the gateways (KEEP it)','(static, keeps ACCEPT) and the gateways (keep ACCEPT)')
edit('tools/probe_features/run.sh','class A — PRODUCTION COMPILE-TIME REFUSAL (A1..A4)','class A — PRODUCTION COMPILE-TIME REFUSAL (A1..A2; legacy A3/A4 retired)')
edit('docs/superpowers/evidence/2026-09-07-radmin-command-authority-table.md','bind to their bare semantic rows; they are not executable subcommands. The gateway join/create\nreboot/prep-restart alias cells retain their own provenance but have the same class/flag as their spellings.', 'bind to their bare semantic rows; they are not executable subcommands. The gateway join/create alias\ncell retains its provenance and the same class/flag as its spellings. Slice 9 removes the radio-only\nreboot/prep-restart metadata row; the ordinary `reboot` and `prep-restart` policies remain.')
p=Path('ios-companion/INBOX_SYNC_CONTRACT.md');s=p.read_text()
s=s.replace('anti-per-keystroke — e2e-ack / rcmd are exempt','anti-per-keystroke — E2E ACK is exempt; remote-admin v2 uses the normal app-DM carrier')
a=s.index('**Ask 1 ⊂ Ask 3**');b=s.index('### Ask 3 —',a)
s=s[:a]+'''**Asks 1 and 3 are historical and SUPERSEDED by remote-admin v2 (Slices 8a+8c/8b).** The current
[controller contract](#remote-controller-local-delivery-slices-8a8c) and
[carrier observations](#remote-controller-carrier-observations-slice-8b) below define the implemented surface.

### Ask 1 — remote-admin app surface — SUPERSEDED in Slice 9

The former request concerned the `rcmd`/password/unlock/lock protocol and binary TLV responses. Slice 9 deletes
that protocol and those verbs. Use `remote`, credential slots and the target book; plaintext NDJSON results,
retention and local ACKs follow the controller contract below. There is no unlocked-password state or legacy
response event to consume.

'''+s[b:]
s=s.replace('### Ask 3 — the CONFIGURATOR path: let a mobile-attached phone configure another node (2026-08-16)', '### Ask 3 — the CONFIGURATOR path (2026-08-16) — historical, superseded by v2')
a=s.index('> ⚠ **This section is NEGOTIABLE',s.index('### Ask 3'));b=s.index('**Product context',a)
s=s[:a]+'''> Historical diagnosis and requests below describe the pre-v2 firmware. They are retained as the motivation,
> not current capability or outstanding work. Mobile CLIENT / static-and-gateway ACCEPT, authenticated sessions
> and the transport-scoped result contract are implemented; Slice 9 removes the old protocol.

'''+s[b:]
a=s.index('**2. Route the `rcmd` RESPONSE');b=s.index('**3. Challenge',a)
s=s[:a]+'''**2. Transport-scoped responses — fulfilled by v2.** `firmware_remote_client.cpp` offers plaintext results
on the requesting USB or secured BLE transport. USB uses `> remote <id> …`; BLE uses `remote_output` and
`remote_terminal` NDJSON. Complete results remain retained until the local delivery/ACK contract is satisfied;
a disconnect is not an acknowledgement. This replaces the old USB-only printer.

'''+s[b:]
a=s.index('- **Remote management: `rcmd');b=s.index('\n- **',a+3)
s=s[:a]+'''- **Remote administration v2 (`remote`, mobile CLIENT only).** Open diagnostics and authenticated execute use
  the target book and provisioned credential slots. USB/BLE receive plaintext, transport-scoped results via
  `remote_output` / `remote_terminal`, with retained-result and explicit BLE `remote-ack` handling. See the
  [controller contract](#remote-controller-local-delivery-slices-8a8c) and
  [carrier observations](#remote-controller-carrier-observations-slice-8b). Slice 9 deletes `rcmd`, password,
  unlock/lock, the counter-floor codec and binary TLV replies; those commands now return the ordinary unknown-verb error.'''+s[b:]
s=s.replace('The board carrier is unavailable until 8b: a local `carrier_unavailable` refusal claims no transmission or\nremote execution. 8a+8c supplies controller state, framing and local delivery; custody and on-air forwarding\nremain separate carrier work.', 'Slice 8b supplies the mobile-to-home wrapper carrier. Local submission acknowledges admission to the\ncontroller\'s own queue only; it does not claim radio delivery or remote execution. Custody observations below\nremain separate from authenticated RPC completion.')
s=s.replace("The controller's board carrier is deliberately unavailable until 8b; this slice's real console\nadmission therefore returns the typed `carrier_unavailable` refusal without putting bytes on air.", 'The earlier 8a+8c `carrier_unavailable` stub was replaced in 8b; a carrier refusal still claims no transmission.')
p.write_text(s)
p=Path('docs/2026-07-31-bench-test-script.md');s=p.read_text();a=s.index('  - **R-RA-27 suspension,');b=s.index('  - Pass: `sf_list',a)
s=s[:a]+'''  - **REMOVED in Slice 9:** the former `rcmd <id> cfg` step and its R-RA-27 suspension are historical.
    The old checked result did not prove support after Slice 1b; Slice 5 replaced target execution and 8a
    replaced response handling. Slice 9 deletes the remaining issuer/protocol. Local USB/companion `cfg`
    remains active; the v2 replacement round trip is Part 57c, with deletion checks in Part 57e.
'''+s[b:]
s=s.replace('Existing Parts 54/58/59/61/62 and the suspended legacy\nstatic/gateway `rcmd` round trip are unchanged.', 'Existing Parts 54/58/59/61/62 are unchanged. Slice 9 removes the legacy\nstatic/gateway `rcmd` round trip; its v2 replacement is Part 57c.')
s+='''

## Part 57e — legacy remote-admin deletion (Slice 9)

**IMPLEMENTATION GATE PENDING / METAL PENDING — NOT RUN.** Run after independent Slice 9 software PASS,
using one static/gateway ACCEPT board and one mobile CLIENT board. Host tests prove dispatcher fallthrough;
this part checks the real USB/BLE handlers and compiled help, then confirms the existing radio round trip.

1. On each board, USB `help` lists none of `rcmd`, `password`, `unlock` or `lock`. BLE `help` continues to
   answer exactly `{"err":"help","msg":"console_only"}`; it must not stream the USB help text.
2. On each board, submit `rcmd 1 status`, `password x`, `unlock x`, and `lock`, one at a time over USB.
   Each answers exactly `> parse error`. Repeat over secured BLE: each answers exactly
   `{"err":"parse","msg":"unknown_cmd"}`. No success-looking legacy reply or remote transmission follows.
3. On the provisioned mobile/controller and ACCEPT target, run Part 57c step 1:
   `remote <label> -e -- status`. Expect the normal `> remote <id16> out …` lines and
   `> remote <id16> completed` (BLE: `remote_output`, then `remote_terminal` with `result:"completed"`).
   Preserve Part 57c's wrapper/source-hash observations; this step does not replace that part's other checks.

Record the two firmware identities, board models, transport transcripts and owner-reported result. The old
`/mrcfg` admin fields and Node mirrors remain inert until Slice 10; no migration or flash-wipe check belongs here.
'''
p.write_text(s)
