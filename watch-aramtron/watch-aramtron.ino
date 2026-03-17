// An Accutron-style smooth sweep that uses the portable microphase driver to
// move the outer hand continuously at about 1 RPM. The position is updated on
// every carrier callback so the commanded velocity never changes.

#include <Arduino.h>
#include <TimerOne.h>

#include "microphase_motor_driver.h"

// Uncomment to drive a scope probe high for the duration of the carrier ISR.
// #define CALLBACK_BENCHMARK_PIN 12

const uint8_t PHASE_COUNT = 6;
const uint8_t CONTACT_COUNT = 4;
const uint32_t SUBPHASES_PER_PHASE = 256UL;
const uint32_t COARSE_PHASES_PER_ROTATION = 360UL * 3UL;
const uint32_t SUBPHASES_PER_ROTATION =
    COARSE_PHASES_PER_ROTATION * SUBPHASES_PER_PHASE;

// 217 us is the nearest integer TimerOne period that advances exactly one
// logical subphase per callback at about 1 RPM.
const uint32_t CARRIER_PERIOD_US = 217UL;
const uint32_t SUBPHASE_INCREMENT_PER_CALLBACK = 1UL;

static_assert(SUBPHASE_INCREMENT_PER_CALLBACK > 0UL,
              "subphase increment must be positive");

#define OUTER_PIN_1 9
#define OUTER_PIN_2 8
#define OUTER_PIN_3 3
#define OUTER_PIN_4 2

const int8_t OUTER_DIRECTION = -1;

struct outer_motor_spec : microphase_motor_spec_t<CONTACT_COUNT, PHASE_COUNT> {
  static const uint8_t pins[pin_count];
  static const uint8_t phases[phase_count][pin_count];
};

const uint8_t outer_motor_spec::pins[outer_motor_spec::pin_count] = {
  OUTER_PIN_1,
  OUTER_PIN_2,
  OUTER_PIN_3,
  OUTER_PIN_4,
};

const uint8_t outer_motor_spec::phases[outer_motor_spec::phase_count]
                                      [outer_motor_spec::pin_count] = {
  {1, 0, 0, 1},
  {0, 0, 0, 1},
  {0, 1, 1, 1},
  {0, 1, 1, 0},
  {1, 1, 1, 0},
  {1, 0, 0, 0},
};

microphase_motor_driver<outer_motor_spec> driver;

volatile uint32_t currentSubphasePosition = 0UL;
volatile uint8_t currentPhaseA = 0U;
volatile uint8_t currentPhaseB = 0U;
volatile uint8_t currentBlendValue = 0U;

inline uint8_t wrapPhaseIndex(int16_t value) {
  value %= PHASE_COUNT;
  if (value < 0) {
    value += PHASE_COUNT;
  }
  return (uint8_t)value;
}

inline uint8_t phaseFromCount(uint32_t phaseCount) {
  return wrapPhaseIndex(
      (int16_t)(OUTER_DIRECTION * (int32_t)(phaseCount % PHASE_COUNT)));
}

void publishOuterPosition(uint32_t subphasePosition) {
  const uint32_t wrappedSubphasePosition =
      subphasePosition % SUBPHASES_PER_ROTATION;
  const uint32_t phaseCount = wrappedSubphasePosition / SUBPHASES_PER_PHASE;
  const uint8_t phaseA = phaseFromCount(phaseCount);
  const uint8_t phaseB = wrapPhaseIndex((int16_t)phaseA + OUTER_DIRECTION);
  const uint8_t blendValue =
      (uint8_t)(wrappedSubphasePosition % SUBPHASES_PER_PHASE);

  if (phaseA != currentPhaseA || phaseB != currentPhaseB) {
    currentPhaseA = phaseA;
    currentPhaseB = phaseB;
    currentBlendValue = blendValue;
    driver.template publish<0>(phaseA, phaseB, blendValue);
    return;
  }

  if (blendValue != currentBlendValue) {
    currentBlendValue = blendValue;
    driver.template publish<0>(blendValue);
  }
}

void onCarrierTick() {
#ifdef CALLBACK_BENCHMARK_PIN
  digitalWriteFast(CALLBACK_BENCHMARK_PIN, HIGH);
#endif

  currentSubphasePosition += SUBPHASE_INCREMENT_PER_CALLBACK;
  if (currentSubphasePosition >= SUBPHASES_PER_ROTATION) {
    currentSubphasePosition -= SUBPHASES_PER_ROTATION;
  }

  publishOuterPosition(currentSubphasePosition);
  driver.tick();

#ifdef CALLBACK_BENCHMARK_PIN
  digitalWriteFast(CALLBACK_BENCHMARK_PIN, LOW);
#endif
}

void setup() {
  driver.begin();

#ifdef CALLBACK_BENCHMARK_PIN
  pinModeFast(CALLBACK_BENCHMARK_PIN, OUTPUT);
  digitalWriteFast(CALLBACK_BENCHMARK_PIN, LOW);
#endif

  publishOuterPosition(0UL);

  Timer1.initialize(CARRIER_PERIOD_US);
  Timer1.attachInterrupt(onCarrierTick);
}

void loop() {}
