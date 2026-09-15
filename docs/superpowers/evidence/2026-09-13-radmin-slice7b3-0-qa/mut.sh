#!/bin/bash
S=/tmp/claude-1001/-home-staszek-MeshRoute/29a5e6f2-869e-46c1-aa18-9fb7b0d23189/scratchpad/s7b30gate; ST=/home/staszek/mr-slice7b30-qa-stage
rsync -a --delete --exclude=.git --exclude=.pio --exclude=.pio-measure --exclude=.claude /home/staszek/MeshRoute/ $ST/ || { echo "rsync failed"; exit 9; }
cd $ST
sha256sum lib/core/remote_codec.h lib/core/remote_codec.cpp test/test_remote_codec.cpp tools/probe_ui_model_mutations.py | cut -c1-16,65- > $S/mut_stage_hashes.txt
for t in radmin2codec radmin5rx a0rx b134ack b134inbox b134ram b134store b159mac b159map b159rx b161hash b161mac b161rx b20codec b20mac b251hash b251rx cmdauthority consoleline devicenv fwactivation grantadmit grantpark macwait radmin3acl radmin3id radmin3verbs radmin5runtime radmin5session radmin72rx radmin72session radmin7exec radmin7rx radmin7transcript remoteactivation sliceAinbox sliceAjson sliceBmac sliceBnode sliceBrx sliceCinbox sliceCpull sliceDack sliceDclear sliceDstore sliceDtoken sliceEnode sliceGinbox sliceGjson sliceGrx teamgrant teamkeyring; do
  echo "== $t $(date +%H:%M:%S)"; python3 tools/probe_ui_model_mutations.py --target=$t --workers=3 > $S/mut_$t.log 2>&1; echo "rc=$?"; grep -E "^mutations:|FAIL |UNUSABLE|VACUOUS|survivor|instrument|baseline" $S/mut_$t.log | tail -4
done
sha256sum lib/core/remote_codec.h lib/core/remote_codec.cpp test/test_remote_codec.cpp tools/probe_ui_model_mutations.py | cut -c1-16,65- > $S/mut_stage_hashes_after.txt
touch $S/mut.done
