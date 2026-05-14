#include "learning/vla_stub.h"

#include <Arduino.h>

namespace node0 {

VlaStub &VlaStub::instance() {
  static VlaStub inst;
  return inst;
}

void VlaStub::begin() {
  // TODO: allocate PSRAM buffers via ps_malloc for dataset storage.
}

void VlaStub::observe(const nodemesh::ExperiencePacket &packet) {
  (void)packet;
  // TODO: store packet samples for later training/inference.
}

void VlaStub::trainStep() {
  // TODO: implement lightweight KNN/MLP update.
}

} // namespace node0
