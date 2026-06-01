#include "app_config.h"
#include "comms/cam_input.h"
#include "comms/uart_input.h"
#include "ik/ik_solver.h"
#include "learning/il_trainer.h"
#include "motion/motion_controller.h"
#include "motion/stepper_sync.h"
#include "storage/sd_logger.h"
#include <Arduino.h>
#include <Preferences.h>
#include <array>
#include <esp_task_wdt.h>

// ---------------------------------------------------------------------------
// Operating modes
// ---------------------------------------------------------------------------
enum class NodeMode : uint8_t {
  kTeleopLog = 0,  // Human drives arm, data recorded to SD.
  kInfer     = 1,  // IL policy drives arm autonomously.
};

namespace {
constexpr uint32_t kControlPeriodUs = 1000000UL / node0::kControlLoopHz;
constexpr uint32_t kWatchdogTimeoutSec = 5;

NodeMode g_mode = NodeMode::kTeleopLog;
Preferences g_prefs;

// ---------------------------------------------------------------------------
// NVS mode persistence
// ---------------------------------------------------------------------------
void loadMode() {
  g_prefs.begin("nodemesh", true);
  g_mode = static_cast<NodeMode>(g_prefs.getUChar("mode", 0));
  g_prefs.end();
}

void saveMode(NodeMode m) {
  g_mode = m;
  g_prefs.begin("nodemesh", false);
  g_prefs.putUChar("mode", static_cast<uint8_t>(m));
  g_prefs.end();
}

// ---------------------------------------------------------------------------
// Serial mode control interface
// Commands (newline-terminated):
//   "mode teleop"  — switch to TELEOP_LOG
//   "mode infer"   — switch to INFER
//   "status"       — print current mode
// ---------------------------------------------------------------------------
void handleSerialCommands() {
  static String buf;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      buf.trim();
      if (buf == "mode teleop") {
        saveMode(NodeMode::kTeleopLog);
        Serial.println("[Node0][CMD] mode -> TELEOP_LOG");
      } else if (buf == "mode infer") {
        saveMode(NodeMode::kInfer);
        Serial.println("[Node0][CMD] mode -> INFER");
      } else if (buf == "ep start") {
        node0::SdLogger::instance().beginEpisode();
      } else if (buf == "ep stop") {
        node0::SdLogger::instance().endEpisode();
      } else if (buf == "log clear") {
        // Wipe the log file and reset episode state.  This is irreversible.
        // Use before re-collecting demonstrations after a bad session.
        node0::SdLogger::instance().clearLog();
      } else if (buf == "status") {
        Serial.printf("[Node0][CMD] mode=%s  episode=%u  ep_open=%s\n",
                      g_mode == NodeMode::kTeleopLog ? "TELEOP_LOG" : "INFER",
                      static_cast<unsigned>(node0::SdLogger::instance().currentEpisode()),
                      node0::SdLogger::instance().episodeOpen() ? "yes" : "no");
      } else if (buf.length() > 0) {
        Serial.println("[Node0][CMD] unknown command (try: mode teleop / mode infer / ep start / ep stop / status)");
      }
      buf = "";
    } else {
      buf += c;
    }
  }
}
} // namespace

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("[Node0] Orchestrator boot");

  loadMode();
  Serial.printf("[Node0] Loaded mode: %s\n",
                g_mode == NodeMode::kTeleopLog ? "TELEOP_LOG" : "INFER");

  // Watchdog: reset if main loop stalls for >5 s.
  {
    const esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms    = kWatchdogTimeoutSec * 1000U,
        .idle_core_mask = 0,  // don't watch idle tasks
        .trigger_panic  = true,
    };
    esp_task_wdt_init(&wdt_cfg);
  }
  esp_task_wdt_add(nullptr);

  node0::ShoulderMirrorStepper::instance().begin();
  node0::MotionController::instance().begin();

  node0::CamInput::instance().begin();
  node0::UartInput::instance().begin();
  node0::SdLogger::instance().begin();
  node0::IkSolver::instance().begin();
  node0::IlTrainer::instance().begin();

  // Preload training data from closed episodes recorded in previous sessions.
  // This is the key step that makes NodeMesh equivalent to ACT in terms of
  // training data: all past demonstrations are available, not just the last
  // 1.28 s of the live ring buffer.
  node0::IlTrainer::instance().loadFromLog();

  Serial.println("[Node0] Boot complete. Commands: mode teleop / mode infer / ep start / ep stop / status / log clear");
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------
void loop() {
  static uint32_t last_tick_us = 0;
  const uint32_t now_us = micros();
  if ((now_us - last_tick_us) < kControlPeriodUs) {
    return;
  }
  last_tick_us = now_us;

  esp_task_wdt_reset();

  handleSerialCommands();

  node0::CamInput::instance().tick();

  nodemesh::ExperiencePacket packet{};
  const bool got_packet = node0::UartInput::instance().tryRead(packet);

  if (got_packet) {
    node0::CamInput::instance().mergeIntoPacket(packet);

    std::array<float, 6> targets_deg{};

    if (g_mode == NodeMode::kTeleopLog) {
      // IK maps teleop joint state → motor targets.
      if (node0::IkSolver::instance().solveFromPacket(packet, targets_deg)) {
        node0::MotionController::instance().setTargets(targets_deg);

        static uint32_t ik_log_counter = 0;
        if ((++ik_log_counter % 200U) == 0U) {
          Serial.printf("[Node0][IK] base=%.1f sh=%.1f el=%.1f wp=%.1f wy=%.1f gr=%.1f\n",
                        targets_deg[0], targets_deg[1], targets_deg[2],
                        targets_deg[3], targets_deg[4], targets_deg[5]);
        }
      }
      node0::SdLogger::instance().enqueue(packet);
      node0::IlTrainer::instance().observe(packet);

    } else {
      // INFER mode: IL policy generates targets directly.
      node0::IlTrainer::instance().infer(packet, targets_deg);
      node0::MotionController::instance().setTargets(targets_deg);
    }
  }

  // Drive motors one step toward smoothed target every tick.
  node0::MotionController::instance().tick();

  node0::SdLogger::instance().flushOnce();
  node0::IlTrainer::instance().trainStep();
}

