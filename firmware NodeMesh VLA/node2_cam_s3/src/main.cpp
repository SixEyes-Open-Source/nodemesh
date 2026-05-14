#include "board_pins.h"
#include "nodemesh/experience_packet.h"
#include "nodemesh/node_ids.h"
#include "nodemesh/packet_codec.h"
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[Node2] Global CAM node boot");
  // TODO: initialize camera and ESP-NOW transport on kEspNowChannel.
}

void loop() {
  nodemesh::ExperiencePacket packet{};
  packet.source_node = static_cast<uint8_t>(nodemesh::NodeId::kNode2CamGlobal);
  packet.timestamp_us = micros();

  // TODO: fill packet.vision_features from extracted camera embedding.
  nodemesh::finalize_packet(packet);

  // TODO: send packet via ESP-NOW to Node0 MAC.
  delay(33);
}
