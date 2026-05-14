# NodeMeshVLA POC Go/No-Go Checklist

Purpose: make an objective decision to continue, simplify, or pivot without bias.

Decision outputs:
- GO: continue NodeMeshVLA as planned.
- CONDITIONAL GO: continue only after fixing specific blockers.
- NO-GO: pivot to simpler architecture.

## 1) Hard Stop Criteria (Any One = NO-GO)

- Safety cannot be proven for mode transitions under fault conditions.
- Control loop determinism degrades beyond acceptable jitter when logging/comms are active.
- Episode data integrity cannot be guaranteed (timestamp sync, packet completeness, corruption handling).
- System requires hidden external compute to function in normal operation.
- Recovery from power loss causes repeated SD corruption or undefined actuator state.

## 2) Must-Pass Gates Before Expanding Scope

### Gate A: Core Reliability

- [ ] 30-minute continuous run with no watchdog/fault surprises.
- [ ] No uncommanded motion during boot, reconnect, or mode transitions.
- [ ] BLE reconnect does not alter active mode unexpectedly.
- [ ] Node0 remains operational if app disconnects.

Pass rule:
- All boxes checked.

### Gate B: Data Quality

- [ ] Teleop joints and vision features are time-aligned within defined tolerance.
- [ ] Episode boundaries are explicit and recoverable after power interruption.
- [ ] Corrupt packet handling is verified (drop + continue, no crash).
- [ ] At least 20 episodes recorded with no unreadable files.

Pass rule:
- All boxes checked.

### Gate C: Learning Value

- [ ] On-device learner beats a static baseline in at least one measurable task metric.
- [ ] Inference is stable for a full demonstration window (no control collapse).
- [ ] Training/inference can be disabled and the robot still behaves safely.

Pass rule:
- All boxes checked.

### Gate D: Integration Cost

- [ ] Build-flash-test cycle remains under target time.
- [ ] New features do not break at least 80% of regression tests.
- [ ] Fault triage can isolate root cause within acceptable engineering time.

Pass rule:
- All boxes checked.

## 3) Scoring Matrix (0 to 5 Each)

Score each category from 0 (unacceptable) to 5 (excellent):

- Safety robustness:
- Runtime determinism:
- Data integrity:
- Learning gain vs baseline:
- Debuggability:
- Team velocity:
- Hardware stability (thermal/power/noise):
- Demo reliability:

Total score = sum of all categories (max 40).

Decision thresholds:
- 32 to 40: GO
- 24 to 31: CONDITIONAL GO (fix weakest 2 categories first)
- 0 to 23: NO-GO (simplify/pivot)

## 4) Devil's Advocate Questions (Answer Honestly)

- Are we solving a real bottleneck or engineering an impressive architecture for its own sake?
- Would a single-node or offline-training approach deliver 80% value with 20% effort?
- Is distributed learning performance improvement measurable, or assumed?
- Are we spending more time stabilizing infrastructure than improving robot behavior?
- If this fails in a demo, can we gracefully fall back to a safe deterministic mode?

If any answer indicates weak value and high risk, downgrade one decision level.

## 5) Minimum Acceptable POC Definition

The POC is only valid if all are true:

- Runs without external compute dependency for normal operation.
- Supports BLE-only mode control and safe fallback on disconnection.
- Records multiple episodes reliably and can replay/train from stored episodes.
- Demonstrates at least one repeatable behavior improvement from on-device learning.
- Maintains safe actuation boundaries at all times.

## 6) Pivot Options If NO-GO

- Pivot A: Keep NodeMesh comms, remove on-device learning (inference-only deterministic stack).
- Pivot B: Keep single orchestrator node only, disable distributed perception nodes.
- Pivot C: Use offline training pipeline, deploy compact on-device inference only.
- Pivot D: Freeze architecture and focus only on reliability and safety milestones.

## 7) Review Cadence

- Weekly go/no-go review until first stable demo.
- Re-run full checklist after any major architecture change.
- Require written rationale for overriding NO-GO decision.
