# NodeMesh VLA Firmware Checklist

## Done

- [x] Created NodeMesh framework root README and topology summary.
- [x] Created shared packet protocol headers and codec implementation.
- [x] Added shared ring buffer utility for non-blocking queueing.
- [x] Scaffolded PlatformIO projects for node0, node1, node2, node3.
- [x] Added node0 orchestrator main loop scaffold (UART ingest, SD queue, VLA stub hooks).
- [x] Added node0 shoulder mirror stepper scaffold for dual-driver lockstep pulses.
- [x] Added node0 SD SPI wiring doc.
- [x] Added node1 input ADC-to-packet streaming scaffold.
- [x] Added node2/node3 camera node boot and packet stubs.
- [x] Added follower pinout doc for PCB routing.
- [x] Audited follower pin assignments for collisions (no duplicates in current map).
- [x] Verified shared EN is compatible with current control model.
- [x] Verified shared PDN is not compatible with current TMC2209 selection logic without firmware refactor.
- [x] Finalized hardware policy: shared EN + separate PDN.
- [x] Updated follower firmware EN pin config to EN_ALL on GPIO6.
- [x] Created one-page connector legend for travel/bench wiring.
- [x] Created NodeMeshVLA overview doc (public summary + full technical breakdown).
- [x] Created ruthless go/no-go decision checklist for objective POC evaluation.
- [x] Created phased roadmap with entry/exit criteria and cross-cutting tracks.

## To Do Next

- [x] Updated schematic net labels and connector pin table to match EN_ALL on GPIO6.
- [ ] Implement true non-blocking high-speed UART pipeline at 921600 on node0.
- [ ] Implement SD filesystem init and pre-allocated/circular write blocks.
- [ ] Add PSRAM allocation paths (`ps_malloc`) for vision and training datasets.
- [ ] Port IK logic from Python stack to node0 C++ module.
- [ ] Implement ESP-NOW transport and feature-vector extraction on node2/node3.
- [ ] Add packet-level integration tests for node1 -> node0 and cam nodes -> node0.
- [ ] Add fault-injection tests for heartbeat timeout and motor disable behavior.

## Hardware Pre-Flight (PCB)

- [ ] Confirm exact ESP32-S3 module pin availability for chosen board SKU.
- [ ] Confirm SD module voltage domain is 3.3V-safe on all IO.
- [ ] Add pull-ups/series resistors on PDN/UART nets per TMC2209 design.
- [ ] Verify dual-shoulder STEP/DIR trace matching and return paths.
- [ ] Review high-current ground routing for servo and stepper domains.
