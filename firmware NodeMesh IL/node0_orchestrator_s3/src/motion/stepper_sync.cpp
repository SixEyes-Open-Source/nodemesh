#include "motion/stepper_sync.h"

#include "board_pins.h"
#include <Arduino.h>
#include <soc/gpio_reg.h>  // REG_WRITE, GPIO_OUT_W1TS_REG, GPIO_OUT_W1TC_REG

namespace node0 {

namespace {
// Build a bitmask for pins that live in the low GPIO bank (0-31).
// kShoulderA.step and kShoulderB.step are both <32 on the follower board.
constexpr uint32_t kStepMask =
    (1UL << kShoulderA.step) | (1UL << kShoulderB.step);
} // namespace

void ShoulderMirrorStepper::begin() {
  pinMode(kShoulderA.step, OUTPUT);
  pinMode(kShoulderA.dir, OUTPUT);
  pinMode(kShoulderA.en, OUTPUT);

  pinMode(kShoulderB.step, OUTPUT);
  pinMode(kShoulderB.dir, OUTPUT);
  pinMode(kShoulderB.en, OUTPUT);

  setEnable(false);
}

void ShoulderMirrorStepper::setEnable(bool enabled) {
  // TMC2209 EN is active-low on most breakout boards.
  digitalWrite(kShoulderA.en, enabled ? LOW : HIGH);
  digitalWrite(kShoulderB.en, enabled ? LOW : HIGH);
}

void ShoulderMirrorStepper::pulse(bool dir_high) {
  // DIR can be set normally — it only needs to be stable before the STEP edge.
  digitalWrite(kShoulderA.dir, dir_high ? HIGH : LOW);
  digitalWrite(kShoulderB.dir, dir_high ? HIGH : LOW);

  // Raise both STEP pins simultaneously via GPIO set register (single write).
  REG_WRITE(GPIO_OUT_W1TS_REG, kStepMask);
  delayMicroseconds(2);
  // Clear both STEP pins simultaneously via GPIO clear register (single write).
  REG_WRITE(GPIO_OUT_W1TC_REG, kStepMask);
}

} // namespace node0
