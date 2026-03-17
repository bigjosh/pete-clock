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

// Linear duty between adjacent 60-degree phase vectors does not produce a
// constant electrical field angle. This table remaps each subphase to the duty
// that makes the average field angle advance uniformly across the phase span.
const uint8_t fieldAngleBlend[SUBPHASES_PER_PHASE] = {
    0,   1,   2,   4,   5,   6,   7,   8,   9,  11,  12,  13,  14,  15,  16,
   17,  19,  20,  21,  22,  23,  24,  25,  26,  27,  29,  30,  31,  32,  33,
   34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
   50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  61,  62,  63,
   64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,
   79,  80,  81,  82,  83,  84,  85,  85,  86,  87,  88,  89,  90,  91,  92,
   93,  94,  95,  96,  97,  98,  98,  99, 100, 101, 102, 103, 104, 105, 106,
  107, 108, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 118, 119,
  120, 121, 122, 123, 124, 125, 126, 127, 128, 128, 129, 130, 131, 132, 133,
  134, 135, 136, 137, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147,
  147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 157, 158, 159, 160,
  161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 170, 171, 172, 173, 174,
  175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189,
  190, 191, 192, 193, 194, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203,
  204, 205, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219,
  220, 221, 222, 223, 224, 225, 226, 228, 229, 230, 231, 232, 233, 234, 235,
  236, 238, 239, 240, 241, 242, 243, 244, 246, 247, 248, 249, 250, 251, 253,
  254
};

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
      fieldAngleBlend[wrappedSubphasePosition % SUBPHASES_PER_PHASE];

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
