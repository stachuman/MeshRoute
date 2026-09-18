# B413 bounded carrier/correlation reproduction

This is a characterization of the incomplete revision-4 implementation, not a new production test or a full
gate. The fixture uses synthetic pending rows and opaque RPC bodies. The Node carrier, queue, counter allocator,
E2E ring, timeout producer and firmware observer are real. Target authentication/execution is outside this proof.

Candidate identity is `../final-gate-inputs.json`, base `c07b77f`, brief `f82429fe…`. The run used the isolated
`/tmp/mr-s8b-r4-_d7gpj1e/iter-gate` tree after re-synchronizing all 361 candidate production/test/tool/platformio
inputs to those hashes, plus this single additional `test/test_b413_characterization.cpp`. Do not overlay it
onto an active coder/QA checkout.

From an isolated copy of the candidate containing that extra test, run:

```sh
pio test -e native
./.pio/build/native/program -tc='B413*'
MR_B413_SOFT_BINDINGS=1 ./.pio/build/native/program -tc='B413*'
```

The second command exits 0, **1 case /26 assertions /0 failed /2966 deliberately skipped**; see `final.log`.
It establishes two direct target flights with counter 1 and two actual 60-s timeouts attributed to request 1.
The third command changes only binding confidence to claimed and exits 1, **1 case /22 assertions /11 expected
failures /2966 deliberately skipped**; see `soft-control.log`. That counterexample reaches the wrapper branch
and invalidates the direct-flight characterization. It is an input control, not a production-code mutation.

`final-build.log` records the successful build. Earlier scratch logs, including the first fixture build's
missing HAL `emit` override, remain under `../iteration-logs/`. These selected runs do not replace the separate
2966-case native gate and do not assert that the incomplete implementation is correct.
