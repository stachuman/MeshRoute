#!/usr/bin/env bash
# usage: w1c_probe_wave.sh <mode:default|noneg>
cd /home/staszek/MeshRoute; G=.pio-measure/w1c/logs; mode=$1; pids=(); names=()
run() { local name=$1; shift; ( "$@" > "$G/gate5-$name.log" 2>&1; echo "EXIT=$?" >> "$G/gate5-$name.log" ) & pids+=($!); names+=("$name"); }
if [ "$mode" = default ]; then
  run inbox-verbs-default          tools/probe_inbox_verbs/run.sh
  run inbox-verbs-client-explicit  env MR_PROBE_ARM=client tools/probe_inbox_verbs/run.sh
  run console-sink-default         tools/probe_console_sink/run.sh
  run firmware-ui-default          tools/probe_firmware_ui/run.sh
  run custody-usb-default          tools/probe_custody_usb/run.sh
  run ble-line-default             tools/probe_ble_line/run.sh
  run features-default             tools/probe_features/run.sh
  run deferred-actions-default     tools/probe_deferred_actions/run.sh
else
  run inbox-verbs-noneg            tools/probe_inbox_verbs/run.sh --no-neg
  run console-sink-noneg           tools/probe_console_sink/run.sh --no-neg
  run firmware-ui-noneg            tools/probe_firmware_ui/run.sh --no-neg
  run custody-usb-noneg            tools/probe_custody_usb/run.sh --no-neg
  run ble-line-noneg               tools/probe_ble_line/run.sh --no-neg
  run features-noneg               tools/probe_features/run.sh --no-neg
  run deferred-actions-noneg       tools/probe_deferred_actions/run.sh --no-neg
fi
for i in "${!pids[@]}"; do wait "${pids[$i]}"; echo "${names[$i]} $(tail -1 "$G/gate5-${names[$i]}.log")"; done
echo "WAVE $mode DONE"
