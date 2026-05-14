#include "motion/stepper_sync.h"

#include "board_pins.h"
#include <Arduino.h>

namespace node0 {

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
  digitalWrite(kShoulderA.dir, dir_high ? HIGH : LOW);
  digitalWrite(kShoulderB.dir, dir_high ? HIGH : LOW);

  digitalWrite(kShoulderA.step, HIGH);
  digitalWrite(kShoulderB.step, HIGH);
  delayMicroseconds(2);
  digitalWrite(kShoulderA.step, LOW);
  digitalWrite(kShoulderB.step, LOW);
}

} // namespace node0
