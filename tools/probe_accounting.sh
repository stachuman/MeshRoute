# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# tools/probe_accounting.sh — THE SHARED COMPLETENESS COMPARATOR for the probe runners ([[B456]] → [[B459]]).
# ⛔ SOURCED, NEVER RUN. A runner sources it by an absolute path from its repository root
#   (`source "$ROOT/tools/probe_accounting.sh"`), and fails loudly — its own message, exit 2 — if it is not there.
#
# ★ WHAT IS SHARED, AND ONLY THAT (U1): the generic half of an accounting — one terminal-observation recorder, the
#   guard-failure ledger and the final comparison of an EXPECTED set against the observations and the guard ledger.
# ⛔ WHAT IS NOT: no runner's declarations, predicates, mutations, modes or labels. Each runner owns how it builds its
#   expected set (firmware-UI: B227's `ctl` extractor; board-UI: its checked-in identity manifest), which outcome each
#   identity must reach, and its own modes. The message wording is a PARAMETER so that each runner's lines stay its own.
#
# THE FORMATS:
#   expected set   one `id<TAB>accepted-outcome` per line — every identity that must be observed, with the ONE outcome
#                  that counts as success for it.
#   observations   one `id<TAB>outcome[<TAB>detail]` per line — written ONLY by `pa_record`; the detail is diagnostic
#                  and never compared.
#   guard ledger   one line per failed guard — written ONLY by `pa_guard`; a guard is not an identity.
# THE VERDICT (`pa_compare`) fails, and reports, each of: an EMPTY expected set; a DUPLICATE declaration; a MISSING,
#   DUPLICATE or UNKNOWN observation; an observation with the WRONG outcome; and EVERY recorded guard failure. One
#   missing identity plus one duplicate at an unchanged total is therefore still caught — which no count can do.

pa_record() {   # pa_record <observations> <id> <outcome> [<detail>] — THE one terminal-observation path
  if [ $# -ge 4 ]; then printf '%s\t%s\t%s\n' "$2" "$3" "$4" >> "$1"
  else                  printf '%s\t%s\n' "$2" "$3" >> "$1"
  fi
}

pa_guard() {   # pa_guard <guard ledger> <what failed>
  printf '%s\n' "$2" >> "$1"
}

pa_nonempty() {   # pa_nonempty <expected> <tag> <noun> — an empty expected set proves nothing, so it fails
  if [ "$(grep -c '' "$1")" -eq 0 ]; then
    echo "  FAIL $2 no $3 is declared — the extractor read nothing, so completeness proves nothing"; return 1
  fi
}

# pa_compare <expected> <observations> <guard ledger> <tag> <noun> <reached> <summary>
#   <tag>      the runner's finding tag, e.g. `[[B456]]`       <noun>     what one identity is, e.g. `control`
#   <reached>  where a missing identity should have arrived, e.g. `ctl`
#   <summary>  the leading words of the one summary line, e.g. `controls accounted`
pa_compare() {
  pa_nonempty "$1" "$4" "$5" || return 1
  awk -F'\t' -v guards="$3" -v tag="$4" -v noun="$5" -v reached="$6" -v summary="$7" '
    FILENAME == ARGV[1] { if ($1 in want) dupdecl[$1] = 1; want[$1] = $2; order[++n] = $1; next }
    { seen[$1]++; got[$1] = $2; if (!($1 in want)) unknown[$1] = 1 }
    END {
      bad = 0
      for (l in dupdecl) { printf "  FAIL %s label DECLARED twice — its verdicts cannot be told apart: %s\n", tag, l; bad++ }
      for (i = 1; i <= n; i++) { l = order[i]
        if (!(l in seen))      { printf "  FAIL %s MISSING verdict — the %s never reached %s: %s\n", tag, noun, reached, l; bad++ }
        else if (seen[l] > 1)  { printf "  FAIL %s DUPLICATE verdict (%d) for: %s\n", tag, seen[l], l; bad++ }
        else if (got[l] != want[l]) { printf "  FAIL %s outcome %s is not the accepted %s for: %s\n", tag, got[l], want[l], l; bad++ }
        else okn++ }
      for (l in unknown)     { printf "  FAIL %s UNKNOWN label — no such %s is declared: %s\n", tag, noun, l; bad++ }
      g = 0; while ((getline line < guards) > 0) { printf "  FAIL %s a %s guard failed: %s\n", tag, noun, line; g++ }
      printf "%s: %d declared, %d exactly once with the accepted outcome, %d guard failure(s)\n", summary, n, okn, g
      exit (bad + g) ? 1 : 0
    }' "$1" "$2"
}
