cd /home/staszek/MeshRoute
S=/tmp/claude-1001/-home-staszek-MeshRoute/7019fa4d-8324-4b18-b429-3c8de8a32587/scratchpad
echo "HEAD: $(git rev-parse HEAD)"; echo "sim: $(git -C ../lora-universal-simulator rev-parse HEAD) status-lines=$(git -C ../lora-universal-simulator status --porcelain | wc -l)"
echo "== fenced files (sha256, lines)"
for f in lib/core/node.cpp lib/core/node.h src/firmware_commands.cpp src/fw_main.cpp lib/console/console_parse.cpp test/test_node_r3.cpp test/test_node_hashlocate.cpp test/test_dual_layer.cpp tools/probe_inbox_verbs/probe_main.cpp tools/probe_inbox_verbs/run.sh tools/probe_ui_model_mutations.py; do echo "$(sha256sum "$f" | cut -d' ' -f1) $(wc -l < "$f") $f"; done
echo "== brief"; sha256sum docs/superpowers/plans/2026-09-25-standalone-mobile-home-w1c-no-default-name.md
echo "== preparation set"; sha256sum -c $S/w1c-prep.txt
echo "== pre-check SUMS: $(cd docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w1c-precheck && sha256sum -c SHA256SUMS 2>&1 | grep -c ': OK$') OK"
echo "== review receipt"; sha256sum docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w1c-brief-review.md docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w1c-brief-review/SHA256SUMS; echo "review SUMS: $(cd docs/superpowers/evidence/2026-09-25-standalone-mobile-home-w1c-brief-review && sha256sum -c SHA256SUMS 2>&1 | grep -c ': OK$') OK"
echo "== tracked inventory"; sha256sum docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md; git diff --quiet HEAD -- docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md && echo "inventory unchanged vs HEAD"
echo "== git status"; git status --porcelain --untracked-files=all
