A=/tmp/w7w8-stop-qa-oykg0j_x
step() { local name=$1; shift; local t0=$(date +%s); "$@" > "$A/final-$name.log" 2>&1; local rc=$?
         printf '%s\t%s\t%s\n' "$name" "$rc" "$(( $(date +%s) - t0 ))" >> "$A/final-runs.tsv"; }
step qa-failed-step false
step qa-following-step true
echo DONE
