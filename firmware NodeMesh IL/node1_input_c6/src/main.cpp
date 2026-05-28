#include "adc_reader.h"
#include "board_pins.h"
#include "nodemesh/experience_packet.h"
#include "nodemesh/node_ids.h"
#include "nodemesh/packet_codec.h"
#include <Arduino.h>

namespace {
node1::AdcReader g_adc;
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(921600, SERIAL_8N1, node1::kUartRxFromNode0, node1::kUartTxToNode0);
  g_adc.begin();
  delay(100);
  Serial.println("[Node1] Input node boot");
}

void loop() {
  nodemesh::ExperiencePacket packet{};
  packet.source_node = static_cast<uint8_t>(nodemesh::NodeId::kNode1Input);
  packet.timestamp_us = micros();

  const auto joints = g_adc.readJointAngles();
  for (size_t i = 0; i < joints.size(); ++i) {
    packet.joints[i] = joints[i];
  }

  static uint32_t s_seq = 0;
  packet.seq = ++s_seq;
  nodemesh::finalize_packet(packet);
  Serial1.write(reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
  delay(4);
}
