#include "board_pins.h"
#include "nodemesh/experience_packet.h"
#include "nodemesh/node_ids.h"
#include "nodemesh/packet_codec.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_now.h>
#include <string.h>

namespace {
bool g_peer_ready = false;
uint32_t g_send_ok = 0;
uint32_t g_send_fail = 0;

void onEspNowSend(const uint8_t *, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    ++g_send_ok;
  } else {
    ++g_send_fail;
  }
}

bool initCamera() {
  camera_config_t cfg{};
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.pin_d0 = node2::kCamPinY2;
  cfg.pin_d1 = node2::kCamPinY3;
  cfg.pin_d2 = node2::kCamPinY4;
  cfg.pin_d3 = node2::kCamPinY5;
  cfg.pin_d4 = node2::kCamPinY6;
  cfg.pin_d5 = node2::kCamPinY7;
  cfg.pin_d6 = node2::kCamPinY8;
  cfg.pin_d7 = node2::kCamPinY9;
  cfg.pin_xclk = node2::kCamPinXclk;
  cfg.pin_pclk = node2::kCamPinPclk;
  cfg.pin_vsync = node2::kCamPinVsync;
  cfg.pin_href = node2::kCamPinHref;
  cfg.pin_sscb_sda = node2::kCamPinSiod;
  cfg.pin_sscb_scl = node2::kCamPinSioc;
  cfg.pin_pwdn = node2::kCamPinPwdn;
  cfg.pin_reset = node2::kCamPinReset;
  cfg.xclk_freq_hz = 10000000;
  cfg.pixel_format = PIXFORMAT_GRAYSCALE;
  cfg.frame_size = FRAMESIZE_QQVGA;
  cfg.jpeg_quality = 12;
  cfg.fb_count = 1;

  const esp_err_t err = esp_camera_init(&cfg);
  return err == ESP_OK;
}

void extractFeatures(camera_fb_t *fb, uint8_t out[128]) {
  memset(out, 0, 128);
  if (fb == nullptr || fb->buf == nullptr || fb->len == 0) {
    return;
  }

  // 8x8 spatial grid of mean brightness over a QQVGA (160x120) frame.
  // Cell (col, row) maps to out[row*8 + col], values 0-255.
  // out[64..127] reserved/zero for future use.
  constexpr size_t kCols      = 8;
  constexpr size_t kRows      = 8;
  constexpr size_t kFrameW    = 160;
  constexpr size_t kFrameH    = 120;
  constexpr size_t kCellW     = kFrameW / kCols;  // 20 px
  constexpr size_t kCellH     = kFrameH / kRows;  // 15 px
  constexpr size_t kCellPx    = kCellW * kCellH;  // 300 px per cell

  uint32_t sums[kRows * kCols] = {};

  for (size_t y = 0; y < kFrameH; ++y) {
    const size_t row = y / kCellH < kRows ? y / kCellH : kRows - 1;
    for (size_t x = 0; x < kFrameW; ++x) {
      const size_t col = x / kCellW < kCols ? x / kCellW : kCols - 1;
      sums[row * kCols + col] += fb->buf[y * kFrameW + x];
    }
  }

  for (size_t i = 0; i < kRows * kCols; ++i) {
    out[i] = static_cast<uint8_t>(sums[i] / kCellPx);
  }
  // out[64..127] remain zero (reserved).
}

bool initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);

  if (esp_now_init() != ESP_OK) {
    return false;
  }
  esp_now_register_send_cb(onEspNowSend);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, node2::kNode0Mac, 6);
  peer.channel = node2::kEspNowChannel;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    return false;
  }
  return true;
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[Node2] Global CAM node boot");

  const bool cam_ok = initCamera();
  g_peer_ready = initEspNow();
  Serial.printf("[Node2] camera=%s espnow=%s\n", cam_ok ? "ok" : "fail",
                g_peer_ready ? "ok" : "fail");
}

void loop() {
  nodemesh::ExperiencePacket packet{};
  packet.source_node = static_cast<uint8_t>(nodemesh::NodeId::kNode2CamGlobal);
  packet.timestamp_us = micros();

  camera_fb_t *fb = esp_camera_fb_get();
  extractFeatures(fb, packet.vision_features);
  if (fb != nullptr) {
    esp_camera_fb_return(fb);
  }

  static uint32_t s_seq = 0;
  packet.seq = ++s_seq;
  nodemesh::finalize_packet(packet);

  if (g_peer_ready) {
    const esp_err_t err =
        esp_now_send(node2::kNode0Mac, reinterpret_cast<uint8_t *>(&packet),
                     sizeof(packet));
    if (err != ESP_OK) {
      ++g_send_fail;
    }
  }

  static uint32_t loops = 0;
  ++loops;
  if ((loops % 60U) == 0U) {
    Serial.printf("[Node2] tx_ok=%u tx_fail=%u\n",
                  static_cast<unsigned>(g_send_ok),
                  static_cast<unsigned>(g_send_fail));
  }

  delay(33);
}
