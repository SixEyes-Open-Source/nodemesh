#include "storage/sd_logger.h"

#include "board_pins.h"
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>

namespace node0 {

SdLogger &SdLogger::instance() {
  static SdLogger inst;
  return inst;
}

bool SdLogger::begin() {
  SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
  pinMode(kSdCs, OUTPUT);
  digitalWrite(kSdCs, HIGH);

  sd_ready_ = SD.begin(kSdCs, SPI);
  if (!sd_ready_) {
    Serial.println("[Node0][SD] SD.begin failed");
    return false;
  }

  return ensureFileReady();
}

bool SdLogger::enqueue(const nodemesh::ExperiencePacket &packet) {
  const bool ok = queue_.push(packet);
  if (!ok) {
    ++dropped_;
  }
  return ok;
}

bool SdLogger::ensureFileReady() {
  if (!sd_ready_) {
    return false;
  }

  if (log_file_) {
    return true;
  }

  if (!SD.exists(kLogPath)) {
    File create_file = SD.open(kLogPath, FILE_WRITE);
    if (!create_file) {
      Serial.println("[Node0][SD] create log file failed");
      return false;
    }
    // Pre-allocate full log region.
    if (!create_file.seek(kLogBytes - 1)) {
      Serial.println("[Node0][SD] pre-allocate seek failed");
      create_file.close();
      return false;
    }
    if (create_file.write(static_cast<uint8_t>(0)) != 1) {
      Serial.println("[Node0][SD] pre-allocate write failed");
      create_file.close();
      return false;
    }
    create_file.close();
  }

  log_file_ = SD.open(kLogPath, FILE_WRITE);
  if (!log_file_) {
    Serial.println("[Node0][SD] open log file failed");
    return false;
  }

  // Write session header at offset 0 before any packet data.
  writeHeader();

  // Packet data begins immediately after header.
  write_offset_ = kHeaderBytes;
  if (!log_file_.seek(write_offset_)) {
    Serial.println("[Node0][SD] initial seek failed");
    log_file_.close();
    return false;
  }

  Serial.println("[Node0][SD] log ready");
  return true;
}

void SdLogger::writeHeader() {
  // Increment session ID from NVS so each power cycle gets a unique ID.
  Preferences prefs;
  prefs.begin("sdlogger", false);
  const uint32_t session_id = prefs.getUInt("sess_id", 0) + 1;
  prefs.putUInt("sess_id", session_id);
  prefs.end();

  SessionHeader hdr{};
  hdr.magic          = kSessionMagic;
  hdr.session_id     = session_id;
  hdr.start_epoch_s  = millis() / 1000UL;
  hdr.packet_count   = 0;

  if (!log_file_.seek(0)) {
    Serial.println("[Node0][SD] header seek failed");
    return;
  }
  log_file_.write(reinterpret_cast<const uint8_t *>(&hdr), kHeaderBytes);
  log_file_.flush();
  Serial.printf("[Node0][SD] session_id=%u\n", static_cast<unsigned>(session_id));
}

void SdLogger::flushOnce() {
  if (!ensureFileReady()) {
    return;
  }

  nodemesh::ExperiencePacket packet{};
  if (!queue_.pop(packet)) {
    return;
  }

  constexpr size_t kPacketBytes = sizeof(nodemesh::ExperiencePacket);
  // Circular wrap: skip back to just after header when end of region reached.
  if (write_offset_ + kPacketBytes > kLogBytes) {
    write_offset_ = kHeaderBytes;
  }

  if (!log_file_.seek(write_offset_)) {
    Serial.println("[Node0][SD] packet seek failed");
    log_file_.close();
    return;
  }

  const auto written =
      log_file_.write(reinterpret_cast<const uint8_t *>(&packet), kPacketBytes);
  if (written != kPacketBytes) {
    Serial.println("[Node0][SD] packet write failed");
    log_file_.close();
    return;
  }

  write_offset_ += kPacketBytes;
  ++session_packet_count_;
  ++pending_flush_writes_;

  if (pending_flush_writes_ >= kFlushEveryNWrites) {
    // Update packet_count in the session header every batch flush.
    const size_t saved_offset = write_offset_;
    if (log_file_.seek(offsetof(SessionHeader, packet_count))) {
      log_file_.write(reinterpret_cast<const uint8_t *>(&session_packet_count_),
                      sizeof(session_packet_count_));
    }
    log_file_.seek(saved_offset);
    log_file_.flush();
    pending_flush_writes_ = 0;

    static uint32_t log_counter = 0;
    if ((++log_counter % 100U) == 0U) {
      Serial.printf("[Node0][SD] packets=%u dropped=%u offset=%u\n",
                    static_cast<unsigned>(session_packet_count_),
                    static_cast<unsigned>(dropped_),
                    static_cast<unsigned>(write_offset_));
    }
  }
}

void SdLogger::beginEpisode() {
  if (episode_open_) {
    Serial.println("[Node0][SD] ep start ignored: episode already open");
    return;
  }
  ++episode_id_;
  episode_open_ = true;
  writeEpisodeMarker(0);
  Serial.printf("[Node0][SD] episode %u START\n", static_cast<unsigned>(episode_id_));
}

void SdLogger::endEpisode() {
  if (!episode_open_) {
    Serial.println("[Node0][SD] ep stop ignored: no episode open");
    return;
  }
  episode_open_ = false;
  writeEpisodeMarker(1);
  Serial.printf("[Node0][SD] episode %u STOP  packets_so_far=%u\n",
                static_cast<unsigned>(episode_id_),
                static_cast<unsigned>(session_packet_count_));
}

void SdLogger::writeEpisodeMarker(uint8_t event) {
  if (!ensureFileReady()) {
    return;
  }

  constexpr size_t kMarkerBytes = sizeof(EpisodeMarker);
  if (write_offset_ + kMarkerBytes > kLogBytes) {
    write_offset_ = kHeaderBytes;
  }

  if (!log_file_.seek(write_offset_)) {
    Serial.println("[Node0][SD] marker seek failed");
    return;
  }

  EpisodeMarker marker{};
  marker.magic        = kEpisodeMagic;
  marker.episode_id   = episode_id_;
  marker.event        = event;
  marker.timestamp_ms = millis();

  log_file_.write(reinterpret_cast<const uint8_t *>(&marker), kMarkerBytes);
  log_file_.flush();
  write_offset_ += kMarkerBytes;
}

void SdLogger::clearLog() {
  if (episode_open_) {
    Serial.println("[Node0][SD] clearLog: forcing ep stop before clear");
    episode_open_ = false; // don't write a marker — file is about to be deleted
  }

  if (log_file_) {
    log_file_.close();
  }

  if (sd_ready_) {
    SD.remove(kLogPath);
    Serial.println("[Node0][SD] log file deleted");
  }

  // Reset all write-side state.
  write_offset_         = kHeaderBytes;
  session_packet_count_ = 0;
  pending_flush_writes_ = 0;
  episode_id_           = 0;
  episode_open_         = false;
  dropped_              = 0;

  // Re-create and initialise the file so logging can resume immediately.
  ensureFileReady();
  Serial.println("[Node0][SD] log cleared — ready for new demos");
}

void SdLogger::trialStart() {
  if (trial_open_) {
    Serial.println("[Node0][SD] trial start ignored: trial already open");
    return;
  }
  ++trial_id_;
  trial_start_ms_ = millis();
  trial_open_     = true;
  Serial.printf("[Node0][SD] trial %u START\n", static_cast<unsigned>(trial_id_));
}

void SdLogger::trialEnd(bool pass) {
  if (!trial_open_) {
    Serial.println("[Node0][SD] trial end ignored: no trial open");
    return;
  }
  trial_open_ = false;
  const uint32_t duration_ms = millis() - trial_start_ms_;

  if (!sd_ready_) {
    Serial.println("[Node0][SD] trial end: SD not ready, result lost");
    return;
  }

  // Write CSV header on first trial.
  if (!SD.exists(kTrialPath)) {
    File hdr = SD.open(kTrialPath, FILE_WRITE);
    if (hdr) {
      hdr.println("trial_id,episode_id,outcome,duration_ms,session_id");
      hdr.close();
    }
  }

  File f = SD.open(kTrialPath, FILE_APPEND);
  if (!f) {
    Serial.println("[Node0][SD] trial end: failed to open trials.csv");
    return;
  }

  // Read session_id from NVS to annotate which training session produced this.
  Preferences prefs;
  prefs.begin("sdlogger", true);
  const uint32_t sess = prefs.getUInt("sess_id", 0);
  prefs.end();

  char row[64];
  snprintf(row, sizeof(row), "%u,%u,%u,%u,%u",
           static_cast<unsigned>(trial_id_),
           static_cast<unsigned>(episode_id_),
           static_cast<unsigned>(pass ? 1 : 0),
           static_cast<unsigned>(duration_ms),
           static_cast<unsigned>(sess));
  f.println(row);
  f.close();

  Serial.printf("[Node0][SD] trial %u %s  duration=%u ms\n",
                static_cast<unsigned>(trial_id_),
                pass ? "PASS" : "FAIL",
                static_cast<unsigned>(duration_ms));
}

} // namespace node0
