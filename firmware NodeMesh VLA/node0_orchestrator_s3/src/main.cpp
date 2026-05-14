#include "app_config.h"
#include "comms/uart_input.h"
#include "learning/vla_stub.h"
#include "motion/stepper_sync.h"
#include "storage/sd_logger.h"
#include <Arduino.h>

namespace {
node0::ShoulderMirrorStepper g_shoulder;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[Node0] Orchestrator boot");

  g_shoulder.begin();
  g_shoulder.setEnable(true);

  node0::UartInput::instance().begin();
  node0::SdLogger::instance().begin();
  node0::VlaStub::instance().begin();
}

void loop() {
  nodemesh::ExperiencePacket packet{};
  if (node0::UartInput::instance().tryRead(packet)) {
    node0::SdLogger::instance().enqueue(packet);
    node0::VlaStub::instance().observe(packet);
  }

  node0::SdLogger::instance().flushOnce();
  node0::VlaStub::instance().trainStep();

  delay(1);
}
