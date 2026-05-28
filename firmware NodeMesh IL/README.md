# NodeMesh IL Firmware Framework

This directory contains the distributed firmware framework for SixEyes NodeMesh Imitation Learning.

## Node Topology

- `node0_orchestrator_s3`: ESP32-S3 brain node (IK, motion, logging, lightweight learning)
- `node1_input_c6`: ESP32-C6 teleoperation input node (6-channel ADC stream)
- `node2_cam`: ESP32-CAM perception node (global view)
- `node3_cam`: ESP32-CAM perception node (eye-in-hand)
- `shared`: Common packet protocol and utilities
- `docs`: Wiring and pinout references

## Deployment Runbook

- `docs/NODEMESH_FLASHING_AND_DEPLOYMENT.md`: End-to-end build, flash, monitor, and bring-up sequence for Node0 through Node3.

## Goals of This Scaffold

- Keep control-loop code deterministic and non-blocking.
- Use binary packet transport for high-rate links.
- Mirror shoulder stepper pulses for dual-driver synchronization.
- Enable MicroSD circular-buffer logging on node0.
- Train a behavioral cloning policy on-device from teleoperation data and deploy it for autonomous inference.

## Project Goal

NodeMesh is a distributed microcontroller-class platform for on-device imitation learning — teleoperation data collection, on-chip behavioral cloning training, and autonomous policy deployment, entirely within a sub-$30 ESP32 cluster with no external compute.

## Next Implementation Steps

1. Replace stubs with hardware-specific driver implementations.
2. Port IK and action-token generation from existing Python logic.
3. Integrate SD write buffering with pre-allocated blocks.
4. Add end-to-end integration tests using synthetic packet streams.
