# NodeMesh

NodeMesh is the distributed firmware and hardware development track for the SixEyes embodied robotics platform.

This repository hosts NodeMesh-focused assets that are maintained independently from the main SixEyes repository to support faster iteration on multi-node architecture, communication, and deployment workflows.

## Repository Scope

- Firmware architecture and implementation work for NodeMesh VLA
- Node-level documentation, pinout references, and deployment notes
- NodeMeshX1 IC design artifacts and supporting references

## Directory Structure

- firmware NodeMesh VLA/: Multi-node firmware scaffold and implementation for Node0 through Node3, with shared packet protocol code and technical docs
- NodeMeshX1 IC Design (Sensor Packet Router)/: NodeMeshX1 design notes and related IC research material

## Relationship to SixEyes

NodeMesh originated in the SixEyes monorepo and was split into this standalone repository so NodeMesh development can progress with independent versioning, issue tracking, and release cadence.

Upstream organization:
- https://github.com/SixEyes-Open-Source

Canonical NodeMesh repository:
- https://github.com/SixEyes-Open-Source/nodemesh

## Contributing

Contributions are welcome. For significant firmware, protocol, or hardware changes, include:

- A concise technical rationale
- Updated documentation for any interface or pinout changes
- Validation notes (build, flash, and test evidence)

## License

This project follows the license defined for the SixEyes open-source effort. See the repository license file for details.
