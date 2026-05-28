#include "comms/cam_input.h"

#include "calibration_config.h"
#include "nodemesh/node_ids.h"
#include "nodemesh/packet_codec.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <string.h>

namespace node0 {

namespace {
constexpr uint8_t kEspNowChannel = 6;

portMUX_TYPE g_cam_mux = portMUX_INITIALIZER_UNLOCKED;

nodemesh::ExperiencePacket g_node2_packet{};
nodemesh::ExperiencePacket g_node3_packet{};
bool g_has_node2 = false;
bool g_has_node3 = false;
uint32_t g_node2_ms = 0;
uint32_t g_node3_ms = 0;
uint32_t g_rx_ok = 0;
uint32_t g_rx_drop = 0;

void ingestPacket(const uint8_t *data, int data_len) {
  if (data == nullptr || data_len != static_cast<int>(sizeof(nodemesh::ExperiencePacket))) {
    ++g_rx_drop;
    return;
  }

  nodemesh::ExperiencePacket packet{};
  memcpy(&packet, data, sizeof(packet));
  if (!nodemesh::validate_packet(packet)) {
    ++g_rx_drop;
    return;
  }

  const uint32_t now_ms = millis();
  if (packet.source_node == static_cast<uint8_t>(nodemesh::NodeId::kNode2CamGlobal)) {
    g_node2_packet = packet;
    g_has_node2 = true;
    g_node2_ms = now_ms;
    ++g_rx_ok;
    return;
  }

  if (packet.source_node == static_cast<uint8_t>(nodemesh::NodeId::kNode3CamWrist)) {
    g_node3_packet = packet;
    g_has_node3 = true;
    g_node3_ms = now_ms;
    ++g_rx_ok;
    return;
  }

  ++g_rx_drop;
}

#if ESP_IDF_VERSION_MAJOR >= 5
void onEspNowRecv(const esp_now_recv_info_t *, const uint8_t *data, int data_len) {
  portENTER_CRITICAL(&g_cam_mux);
  ingestPacket(data, data_len);
  portEXIT_CRITICAL(&g_cam_mux);
}
#else
void onEspNowRecv(const uint8_t *, const uint8_t *data, int data_len) {
  portENTER_CRITICAL(&g_cam_mux);
  ingestPacket(data, data_len);
  portEXIT_CRITICAL(&g_cam_mux);
}
#endif

} // namespace

CamInput &CamInput::instance() {
  static CamInput inst;
  return inst;
}

bool CamInput::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kEspNowChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[Node0][CAM] esp_now_init failed");
    return false;
  }

  esp_now_register_recv_cb(onEspNowRecv);
  Serial.println("[Node0][CAM] ESP-NOW receive ready");
  return true;
}

void CamInput::tick() {
  static uint32_t last_log_ms = 0;
  const uint32_t now_ms = millis();
  if (now_ms - last_log_ms < 2000U) {
    return;
  }
  last_log_ms = now_ms;

  uint32_t rx_ok = 0;
  uint32_t rx_drop = 0;
  bool has_n2 = false;
  bool has_n3 = false;

  portENTER_CRITICAL(&g_cam_mux);
  rx_ok = g_rx_ok;
  rx_drop = g_rx_drop;
  has_n2 = g_has_node2;
  has_n3 = g_has_node3;
  portEXIT_CRITICAL(&g_cam_mux);

  Serial.printf("[Node0][CAM] rx_ok=%u rx_drop=%u n2=%u n3=%u\n",
                static_cast<unsigned>(rx_ok), static_cast<unsigned>(rx_drop),
                has_n2 ? 1U : 0U, has_n3 ? 1U : 0U);
}

void CamInput::mergeIntoPacket(nodemesh::ExperiencePacket &packet) {
  nodemesh::ExperiencePacket n2{};
  nodemesh::ExperiencePacket n3{};
  bool use_n2 = false;
  bool use_n3 = false;

  const uint32_t now_ms = millis();

  portENTER_CRITICAL(&g_cam_mux);
  if (g_has_node2 && (now_ms - g_node2_ms) <= calib::kVisionStaleMs) {
    n2 = g_node2_packet;
    use_n2 = true;
  }
  if (g_has_node3 && (now_ms - g_node3_ms) <= calib::kVisionStaleMs) {
    n3 = g_node3_packet;
    use_n3 = true;
  }
  portEXIT_CRITICAL(&g_cam_mux);

  if (use_n2 && use_n3) {
    for (size_t i = 0; i < nodemesh::kVisionFeatureBytes; ++i) {
      packet.vision_features[i] = static_cast<uint8_t>(
          (static_cast<uint16_t>(n2.vision_features[i]) +
           static_cast<uint16_t>(n3.vision_features[i])) /
          2U);
    }
    return;
  }

  if (use_n2) {
    memcpy(packet.vision_features, n2.vision_features, nodemesh::kVisionFeatureBytes);
    return;
  }

  if (use_n3) {
    memcpy(packet.vision_features, n3.vision_features, nodemesh::kVisionFeatureBytes);
    return;
  }

  memset(packet.vision_features, 0, nodemesh::kVisionFeatureBytes);
}

} // namespace node0
