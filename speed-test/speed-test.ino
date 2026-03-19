// Sweeps thru a series of maximum rotation speeds on both axles. 
// At the beging of each trial, it accelerates up to the trial speed. 
// Prints the current max V to the serial at the begining of each trial.
// Findings: 60RPM is always reliable. 
// To get above that, you need angular momementum. My bobby pin can reliably run up to 180RPM. 

#include <Arduino.h>
#include <math.h>

const uint8_t PHASE_COUNT = 6;
const uint8_t CONTACT_COUNT = 4;
const uint16_t PHASES_PER_ROTATION = 360U * 3U;

const uint8_t phases[PHASE_COUNT][CONTACT_COUNT] = {
  {1, 0, 0, 1},
  {0, 0, 0, 1},
  {0, 1, 1, 1},
  {0, 1, 1, 0},
  {1, 1, 1, 0},
  {1, 0, 0, 0},
};

const unsigned long SERIAL_BAUD = 115200UL;

#define INNER_PIN_1 5
#define INNER_PIN_2 4
#define INNER_PIN_3 7
#define INNER_PIN_4 6

#define OUTER_PIN_1 9
#define OUTER_PIN_2 8
#define OUTER_PIN_3 3
#define OUTER_PIN_4 2

// The axles are geared opposite mechanically, so driving the electrical phase
// sequence in the same direction makes the hands spin opposite directions.
const int8_t INNER_DIRECTION = 1;
const int8_t OUTER_DIRECTION = 1;

const float START_RPM = 60.0f;
const float END_RPM = 360.0f;
const float RPM_STEP = 10.0f;
const float MAX_ACCELERATION_RPM_PER_S = 60.0f;

const uint32_t HOLD_TIME_MS = 5000UL;
const uint32_t PAUSE_TIME_MS = 1000UL;

uint8_t innerPhaseIndex = 0;
uint8_t outerPhaseIndex = 0;
float currentRpm = 0.0f;
float phaseAccumulator = 0.0f;

uint8_t wrapPhaseIndex(int16_t phaseIndex) {
  while (phaseIndex < 0) {
    phaseIndex += PHASE_COUNT;
  }
  while (phaseIndex >= PHASE_COUNT) {
    phaseIndex -= PHASE_COUNT;
  }
  return (uint8_t)phaseIndex;
}

void writeInnerPhase(uint8_t phaseIndex) {
  digitalWrite(INNER_PIN_1, phases[phaseIndex][0]);
  digitalWrite(INNER_PIN_2, phases[phaseIndex][1]);
  digitalWrite(INNER_PIN_3, phases[phaseIndex][2]);
  digitalWrite(INNER_PIN_4, phases[phaseIndex][3]);
}

void writeOuterPhase(uint8_t phaseIndex) {
  digitalWrite(OUTER_PIN_1, phases[phaseIndex][0]);
  digitalWrite(OUTER_PIN_2, phases[phaseIndex][1]);
  digitalWrite(OUTER_PIN_3, phases[phaseIndex][2]);
  digitalWrite(OUTER_PIN_4, phases[phaseIndex][3]);
}

void writeBothPhases() {
  writeInnerPhase(innerPhaseIndex);
  writeOuterPhase(outerPhaseIndex);
}

float phaseRateFromRpm(float rpm) {
  return (rpm * (float)PHASES_PER_ROTATION) / 60.0f;
}

void stepBothMotors() {
  innerPhaseIndex =
      wrapPhaseIndex((int16_t)innerPhaseIndex + INNER_DIRECTION);
  outerPhaseIndex =
      wrapPhaseIndex((int16_t)outerPhaseIndex + OUTER_DIRECTION);
  writeBothPhases();
}

void advanceMotion(float dtSeconds) {
  phaseAccumulator += phaseRateFromRpm(currentRpm) * dtSeconds;

  while (phaseAccumulator >= 1.0f) {
    stepBothMotors();
    phaseAccumulator -= 1.0f;
  }
}

void moveToRpm(float targetRpm) {
  uint32_t lastMicros = micros();

  while (fabs(currentRpm - targetRpm) > 0.01f) {
    const uint32_t nowMicros = micros();
    const float dtSeconds = (nowMicros - lastMicros) / 1000000.0f;
    if (dtSeconds <= 0.0f) {
      continue;
    }
    lastMicros = nowMicros;

    const float maxDeltaRpm = MAX_ACCELERATION_RPM_PER_S * dtSeconds;
    if (currentRpm < targetRpm) {
      currentRpm = min(currentRpm + maxDeltaRpm, targetRpm);
    } else {
      currentRpm = max(currentRpm - maxDeltaRpm, targetRpm);
    }

    advanceMotion(dtSeconds);
  }
}

void holdCurrentRpm(uint32_t holdTimeMs) {
  const uint32_t holdTimeUs = holdTimeMs * 1000UL;
  const uint32_t startMicros = micros();
  uint32_t lastMicros = startMicros;

  while ((micros() - startMicros) < holdTimeUs) {
    const uint32_t nowMicros = micros();
    const float dtSeconds = (nowMicros - lastMicros) / 1000000.0f;
    if (dtSeconds <= 0.0f) {
      continue;
    }
    lastMicros = nowMicros;
    advanceMotion(dtSeconds);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);

  pinMode(INNER_PIN_1, OUTPUT);
  pinMode(INNER_PIN_2, OUTPUT);
  pinMode(INNER_PIN_3, OUTPUT);
  pinMode(INNER_PIN_4, OUTPUT);

  pinMode(OUTER_PIN_1, OUTPUT);
  pinMode(OUTER_PIN_2, OUTPUT);
  pinMode(OUTER_PIN_3, OUTPUT);
  pinMode(OUTER_PIN_4, OUTPUT);

  writeBothPhases();
}

void loop() {
  for (float targetRpm = START_RPM; targetRpm <= END_RPM; targetRpm += RPM_STEP) {
    Serial.print("trial_rpm=");
    Serial.print(targetRpm, 0);
    Serial.print(" max_phase_rate_s=");
    Serial.println(phaseRateFromRpm(targetRpm), 1);

    moveToRpm(targetRpm);
    holdCurrentRpm(HOLD_TIME_MS);
    moveToRpm(0.0f);
    phaseAccumulator = 0.0f;
    delay(PAUSE_TIME_MS);
  }
}
