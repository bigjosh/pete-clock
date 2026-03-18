#include <Arduino.h>

#define MOTOR_PIN_1 A1
#define MOTOR_PIN_2 A2
#define MOTOR_PIN_3 A3
#define MOTOR_PIN_4 A4

#define PHASE_DELAY_MS 10

const uint8_t PHASE_COUNT = 6;
const uint8_t phases[PHASE_COUNT][4] = {
  {1, 0, 0, 1},
  {0, 0, 0, 1},
  {0, 1, 1, 1},
  {0, 1, 1, 0},
  {1, 1, 1, 0},
  {1, 0, 0, 0},
};

void writePhase(uint8_t phaseIndex) {
  digitalWrite(MOTOR_PIN_1, phases[phaseIndex][0]);
  digitalWrite(MOTOR_PIN_2, phases[phaseIndex][1]);
  digitalWrite(MOTOR_PIN_3, phases[phaseIndex][2]);
  digitalWrite(MOTOR_PIN_4, phases[phaseIndex][3]);
}

void setup() {
  pinMode(MOTOR_PIN_1, OUTPUT);
  pinMode(MOTOR_PIN_2, OUTPUT);
  pinMode(MOTOR_PIN_3, OUTPUT);
  pinMode(MOTOR_PIN_4, OUTPUT);

  writePhase(0);
}

void loop() {
  for (uint8_t phaseIndex = 0; phaseIndex < PHASE_COUNT; ++phaseIndex) {
    writePhase(phaseIndex);
    delay(PHASE_DELAY_MS);
  }
}
