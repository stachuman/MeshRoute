# DISPATCH BRIEF — Wave-4 #2+#3: the `bad_freq` false-success FIX + make `-Wswitch-enum` clean

**★ Read `brief-3b-COMMON.md` FIRST** for boundaries, gate and report format. This is an owner-approved **FIX +
audit**, not a refactor, so C1 does not constrain the behaviour change at the one real defect — but every other
COMMON rule applies.

## Why these two are one slice

`-Wswitch` — the warning the project's zero-tolerance rule is written against — is **blind to any switch
carrying a `default:` label.** The `bad_freq` defect below lives behind exactly such a label, which is why it
survived. So fixing the defect and closing the instrument's blind spot are the same job: get
**`-Wswitch-enum` to zero** and the class is actually closed, rather than closed-against-a-warning-that-cannot-see-it.

★ This is the **fifth** instance of BASELINE 26n's lesson (*"a sweep is only as good as its scope"*) — and the
first where the faulty scope is the **flag**, not the directory.

## Part 1 — THE REAL DEFECT (fix it)

`src/firmware_config.cpp` `gw_parse_err_str` maps **12 of `GwParseErr`'s 14** enumerators and ends
`default: return "ok";`. QA verified: **`ok` and `bad_freq` have no case**, and `bad_freq` is reachable —
`lib/core/node.cpp:178-179` returns it for `gateway … freq0=`/`freq1=` ≤ 0. The caller
(`src/firmware_config.cpp:358`) prints `"> gateway err "` + the string, so **a genuine refusal renders as
`> gateway err ok`.**

⚠ **This is worse than the six holes closed in the previous slice** — those printed *nothing*; this prints a
**false success**, telling the operator a rejected command succeeded.

Fix: give `bad_freq` its own string, give `ok` an explicit case, and **remove the `default:`** so `-Wswitch`
guards this function from now on (keep an unreachable post-switch return — that is the established idiom in
`lib/console/console_json.cpp`). Match the surrounding strings' style; **read all 12 existing cases first** (U3).

## Part 2 — the four `lib/` sites, with QA's classification to VERIFY (I have been wrong repeatedly)

`-Wswitch-enum` over `lib/core` + `lib/console` = **6 warnings at 4 sites.** My reading of each — **check it and
report the truth:**

| site | missing | QA's read | expected action |
|---|---|---|---|
| `lib/core/node_mac.cpp:986` | `rts`, `beacon` | **Intentional and already documented** — the comment says *"rts/beacon NOT retry-eligible"*, `default: return -1` | make the two exclusions **explicit cases** returning `-1`, so the intent is in the code and a future tag warns |
| `lib/core/node_mac.cpp:975` `label_of_frame` | `beacon` | **Correct today, fragile** — `beacon` is served by `default: return "BCN"`, so a **7th `FrameTag` would silently print "BCN"** | explicit `case beacon: return "BCN";`, drop the `default:`, keep an unreachable trailing return |
| `lib/core/node_mac.cpp:130` | `ok`, `cross_layer` | Switch is inside `if (n == 0)` (seal FAILED), so those two outcomes look **unreachable there** — **verify that** | if unreachable: explicit cases that fail loud or assert-comment; if reachable, that is a **BUG — report it** |
| `lib/core/node.cpp:894` | `EXT` | Frame dispatch on `wire::cmd_of()`. **Unverified by me** — determine whether an `EXT` frame is deliberately ignored or silently dropped | if deliberate, an explicit no-op case + comment; **if it is a silent drop of a real frame type, STOP and report** — that is a protocol finding, not a warning |

**Do not force uniformity.** Where a `default:` is genuinely correct (a switch over a *vendor* enum, or over an
integer rather than an enum — `-Wswitch` never applies to those), leave it and say so. The goal is that every
remaining `default:` is a deliberate, documented decision.

## Part 3 — the audit, so the gate can be tightened

- Report the **full `-Wswitch-enum` census before and after**, for `lib/core` + `lib/console` **and** for `src/`
  via the real `pio` build (`src/` cannot be compiled standalone — no Arduino backend).
- Enumerate every remaining `default:` label in those directories and classify each: **enum-exhaustive-and-guarded /
  deliberate-catch-all-with-reason / integer-or-vendor-switch (not applicable)**. QA counted ~17 in `lib/`;
  verify.
- ★ **State whether `-Wswitch-enum` can now join the standing gate at zero.** If some sites legitimately cannot
  reach zero, say which and why — that is the useful answer, and it tells us whether the rule should be
  `-Wswitch-enum`-clean or `-Wswitch`-clean-plus-an-audited-exception-list.

## The gate

Per COMMON §D, plus:
- **`-Wswitch` stays 0** in `lib/core`+`lib/console` **and 0 in the board build** (the previous slice took it
  from 60 → 0 — do not regress it).
- **`-Wswitch-enum`: report before → after.** Target 0; a documented non-zero is acceptable if justified.
- **All 27 scenarios byte-identical.** ⚠ Expect this, and be precise about what it proves: `src/` is invisible to
  the simulator (compilation-guaranteed, per item 4's measurement — no probe needed there). But the **four `lib/`
  sites ARE compiled by `lus`**, so byte-identity there is real evidence — and if a stream moves you changed
  behaviour, most likely at `node_mac.cpp:130` or `node.cpp:894`. **Poison-probe the `lib/` sites** (COMMON §E)
  so you can say which of the four the corpus actually exercises.
- **Native count EXACT** unless you add a test; if you do, say so loudly and give the new numbers.
- **Boards 10/10 + BEFORE/AFTER RAM/Flash**, isolated build dirs, snapshot BEFORE (the tree is dirty with other
  slices — `rsync`, not `git archive HEAD`).
- `sizeof(Node)` unchanged, proven positively.

## Report

Per COMMON §F, with the `-Wswitch-enum` census as the headline and the four-site classification table verified
or corrected. **A premise of mine you disprove is a valuable result** — especially at `node.cpp:894`, which I did
not verify.
