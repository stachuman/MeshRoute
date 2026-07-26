<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# iOS Companion — v1 feature roadmap (2026-07-26)

Purpose: the single index of the companion-side work required for the v1 public release, referencing the
firmware feature specs for the on-node halves. This is a ROADMAP/planning spec — it defines WHAT the
companion must do and points at the authoritative designs; it does NOT re-specify the firmware. The BLE
wire details live in `ios-companion/INBOX_SYNC_CONTRACT.md` (the "Planned for v1" section added 2026-07-26
mirrors this list). No code — specs only.

## Scope ruling (owner, 2026-07-26)
The iOS companion is a **v1 release feature** (ships with the first firmware release), not a fast-follow.
The two new v1 feature arcs both have significant companion halves, ruled in-scope now:
1. **Team encrypted channel + team QR** — `docs/superpowers/specs/2026-07-26-team-encrypted-channel-design.md`
2. **Remote-admin challenge–response** — `docs/superpowers/specs/2026-07-26-remote-admin-challenge-response-design.md`

## A. Team QR onboarding + encrypted channel (companion side)
Firmware authority: the team-encrypted-channel spec, slice **T-K4** (QR) + the send/receive surfaces.
Companion must:
- **Render the team QR** (creator side) behind a deliberate "Share team" screen that WARNS it contains a
  PRIVATE key. Payload per T-K4 §2.4: `ver · team_name · team_id · freq_khz · bw_hz · routing_sf ·
  sf_list bitmap · cr · team_ch_pub · team_ch_priv` + CRC32. (~90 B + name.)
- **Scan + provision** (joiner side): decode → provision the node over the existing companion channel
  (extend the `team …` provisioning verb with `tkpub=/tkpriv=` hex64 per T-K4). One scan = full onboarding
  (PHY params + content key).
- **Grant a key to a vetted newjoiner** (in-app, the sealed-DM path — T-K3): show the team roster; a
  keyholder taps a newjoiner → sends the `TEAM_KEY_GRANT` (DATA_TYPE 19) sealed DM. Surface
  `team_key_received` to the joiner.
- **Lock state per team**: show whether this node holds the team content key (can read encrypted posts) vs
  is an un-keyed overlay member (relays but cannot read — prompt "ask a teammate for the key" on the
  `team_channel_no_key` push).
- **Location on the map** (T-K5): render team members from the encrypted location inner-type; app-driven
  per-send cadence (Q4 of the team spec).

## B. Remote admin (companion side)
Firmware authority: the remote-admin challenge–response spec, slice **RA-3** (full companion slice per its
Q4 ruling — NOT a stub).
Companion must:
- **Unlock/lock**: derive the admin key from a passphrase in-app (or drive the node's `unlock <pw>`), with
  a clear locked/unlocked indicator; `lock` wipes.
- **Challenge lifecycle, made INVISIBLE**: cache the per-node challenge learned from sealed responses;
  **auto-bootstrap** on first contact (send a `challenge==0` command per §2.4); **auto-retry-once** on a
  resync response. The operator should never see or type a challenge — the UX goal is "send command → get
  result", with a silent bootstrap/resync underneath.
- **Open reads vs sealed commands**: open reads (`status`, `routes`) need no unlock and no challenge
  (Q2 ruling); sealed verbs (reboot / config / `password rotate`) require unlock + carry the challenge.
- **Fire-and-observe**: treat `rcmd` as fire-and-observe (per the existing contract note); render the
  sealed-response body; on `admin_challenge_resync`, resync silently.
- (Q3 of the remote-admin spec — piggybacking the challenge onto open-read responses — is DEFERRED; the
  companion relies on the `challenge==0` bootstrap. If bootstrap latency is later measured to matter, Q3
  becomes a small companion+firmware optimization.)

## C. Carried-over v1 companion surface (already in the contract, listed for completeness)
Inbox sync (model B), verified-peer QR pubkey exchange, per-message crypt indicator, leaf-config
provisioning + join feedback, anti-spam limits feedback, position feed (RAM-live / NV-default). These are
IMPLEMENTED or contracted already — see INBOX_SYNC_CONTRACT.md; not re-specified here.

## D. Dependency / sequencing notes
- Both A and B depend on their firmware slices landing first (the companion consumes the firmware surface).
  Firmware order is the owner's call (both arcs are queued in `docs/2026-07-25-agent-handover.md` §6.3b/§6.3c).
- The team QR (A) reuses the EXISTING verified-peer QR machinery in the app (INBOX_SYNC_CONTRACT §"QR
  pubkey exchange") — a second QR *type*, not a new scanner.
- Remote admin (B) reuses the EXISTING sealed-DM + unlock plumbing already noted in the contract's
  "Adjacent BLE surface"; the challenge-response change is a payload evolution, not a new transport.

## E. Promotional relevance (context, not spec)
Both A and B are launch-story assets (see the promo drafts): the team QR = the "onboard your squad in one
scan" demo beat; remote admin from the phone = "manage any node in your mesh from your pocket, securely,
even three hops away." Worth a beat each in the demo video once they land.
