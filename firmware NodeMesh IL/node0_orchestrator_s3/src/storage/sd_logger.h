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

// Written inline in the log stream to mark episode boundaries.
// Offline tools distinguish it from ExperiencePackets and SessionHeaders by magic.
#pragma pack(push, 1)
struct EpisodeMarker {
  uint32_t magic;        // 0x4E4D4550 ("NMEP")
  uint32_t episode_id;   // Monotonically incrementing within the session.
  uint8_t  event;        // 0 = start, 1 = stop.
  uint8_t  reserved0;
  uint16_t reserved1;
  uint32_t timestamp_ms; // millis() at the moment the command was issued.
  uint8_t  reserved2[16];
};
#pragma pack(pop)

static_assert(sizeof(EpisodeMarker) == 32, "EpisodeMarker size mismatch");

class SdLogger {
public:
  static SdLogger &instance();
  bool begin();
  bool enqueue(const nodemesh::ExperiencePacket &packet);
  void flushOnce();

  // Episode boundary control (called from serial command handler).
  // beginEpisode() is a no-op if an episode is already open.
  // endEpisode() is a no-op if no episode is open.
  void beginEpisode();
  void endEpisode();

  // Delete the log file and start fresh.  Resets all episode / packet state.
  // Use over serial ('log clear') to discard bad demonstrations before
  // re-collecting.  Irreversible — confirm before calling.
  void clearLog();

  // Trial outcome logging for success-rate measurement.
  // trial start   — begin a timed trial, records start_ms in CSV
  // trial pass/fail — close trial with outcome; appends row to /trials.csv
  // CSV columns: trial_id, episode_id, outcome(pass=1/fail=0),
  //              duration_ms, dataset_n, session_id
  void trialStart();
  void trialEnd(bool pass);

  uint32_t trialCount()   const { return trial_id_; }
  bool     trialOpen()    const { return trial_open_; }

  uint32_t droppedPackets()  const { return dropped_; }
  uint32_t currentEpisode()  const { return episode_id_; }
  bool     episodeOpen()     const { return episode_open_; }

private:
  SdLogger() = default;

  bool ensureFileReady();
  void writeHeader();
  void writeEpisodeMarker(uint8_t event);

  static constexpr const char *kLogPath      = "/node0_log.bin";
  static constexpr size_t     kLogBytes      = 64UL * 1024UL * 1024UL; // 64 MB
  static constexpr size_t     kHeaderBytes   = sizeof(SessionHeader);
  static constexpr uint8_t    kFlushEveryNWrites = 8;
  static constexpr uint32_t   kSessionMagic  = 0x4E4D4C47UL;
  static constexpr uint32_t   kEpisodeMagic  = 0x4E4D4550UL;

  nodemesh::RingBuffer<nodemesh::ExperiencePacket, calib::kPacketQueueDepth> queue_;
  File     log_file_;
  bool     sd_ready_             = false;
  size_t   write_offset_         = kHeaderBytes; // data starts after header
  uint8_t  pending_flush_writes_ = 0;
  uint32_t session_packet_count_ = 0;
  uint32_t dropped_              = 0;
  uint32_t episode_id_           = 0;
  bool     episode_open_         = false;

  // Trial state
  static constexpr const char *kTrialPath = "/trials.csv";
  uint32_t trial_id_             = 0;
  uint32_t trial_start_ms_       = 0;
  bool     trial_open_           = false;
};

} // namespace node0
