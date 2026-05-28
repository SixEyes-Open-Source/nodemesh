#pragma once

#include "calibration_config.h"
#include "nodemesh/experience_packet.h"
#include "nodemesh/ring_buffer.h"
#include <SD.h>
#include <stddef.h>
#include <stdint.h>

namespace node0 {

// Fixed-size header written at offset 0 of the log file.
// Lets offline tools know where a session started and how many packets landed.
#pragma pack(push, 1)
struct SessionHeader {
  uint32_t magic;          // 0x4E4D4C47 ("NMLG")
  uint32_t session_id;     // Monotonically incrementing, persisted in NVS.
  uint32_t start_epoch_s;  // Seconds since boot (millis()/1000) at session open.
  uint32_t packet_count;   // Updated on each flush batch.
  uint8_t  reserved[16];   // Pad to 28 bytes for future fields.
};
#pragma pack(pop)

static_assert(sizeof(SessionHeader) == 32, "SessionHeader size mismatch");

class SdLogger {
public:
  static SdLogger &instance();
  bool begin();
  bool enqueue(const nodemesh::ExperiencePacket &packet);
  void flushOnce();

  uint32_t droppedPackets() const { return dropped_; }

private:
  SdLogger() = default;

  bool ensureFileReady();
  void writeHeader();

  static constexpr const char *kLogPath      = "/node0_log.bin";
  static constexpr size_t     kLogBytes      = 64UL * 1024UL * 1024UL; // 64 MB
  static constexpr size_t     kHeaderBytes   = sizeof(SessionHeader);
  static constexpr uint8_t    kFlushEveryNWrites = 8;
  static constexpr uint32_t   kSessionMagic  = 0x4E4D4C47UL;

  nodemesh::RingBuffer<nodemesh::ExperiencePacket, calib::kPacketQueueDepth> queue_;
  File     log_file_;
  bool     sd_ready_             = false;
  size_t   write_offset_         = kHeaderBytes; // data starts after header
  uint8_t  pending_flush_writes_ = 0;
  uint32_t session_packet_count_ = 0;
  uint32_t dropped_              = 0;
};

} // namespace node0
