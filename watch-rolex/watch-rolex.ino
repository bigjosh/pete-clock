// A Rolex-style watch face driven through the shared portable subphase driver.
// The second hand advances exactly 8 times per second, and the minute hand
// advances on every same beat by 1/60th as much on average - just like a real
// Rolex. It uses our new microphase framework to be able to hit all of those
// positions, many of which do not land exactly on even phases locations.

#include <Arduino.h>
#include <TimerOne.h>

#include "microphase_motor_driver.h"

// Uncomment to drive a scope probe high for the duration of the carrier ISR.
#define CALLBACK_BENCHMARK_PIN 12

const uint8_t PHASE_COUNT = 6;
const uint8_t CONTACT_COUNT = 4;
const uint32_t SUBPHASES_PER_PHASE = 256UL;

const uint32_t COARSE_PHASES_PER_ROTATION = 360UL * 3UL;
const uint32_t SUBPHASES_PER_ROTATION =
    COARSE_PHASES_PER_ROTATION * SUBPHASES_PER_PHASE;

const uint32_t SECOND_HAND_TICKS_PER_SECOND = 8UL;
const uint32_t SECOND_HAND_TICKS_PER_ROTATION =
    60UL * SECOND_HAND_TICKS_PER_SECOND;
const uint32_t MINUTE_HAND_TICKS_PER_ROTATION =
    60UL * SECOND_HAND_TICKS_PER_ROTATION;
const uint32_t TICK_PERIOD_US = 1000000UL / SECOND_HAND_TICKS_PER_SECOND;
const uint32_t CARRIER_PERIOD_US = 32UL;
const uint32_t MAX_PUBLISH_DELTA_SUBPHASES = SUBPHASES_PER_PHASE - 1UL;

const uint32_t OUTER_SUBPHASES_PER_TICK =
    SUBPHASES_PER_ROTATION / SECOND_HAND_TICKS_PER_ROTATION;
const float OUTER_AVG_SUBPHASES_PER_TICK = (float)OUTER_SUBPHASES_PER_TICK;
const float INNER_AVG_SUBPHASES_PER_TICK =
    (float)SUBPHASES_PER_ROTATION / (float)MINUTE_HAND_TICKS_PER_ROTATION;
const float TICK_PERIOD_SECONDS = TICK_PERIOD_US / 1000000.0f;

// These are the motion limits for the foreground chaser, not the TimerOne
// carrier. We respond to each 8 Hz tick by accelerating toward the next
// commanded position, then clamping hard to rest when we arrive.
const float OUTER_MAX_ACCELERATION_SUBPHASES_PER_S2 =
    (4.0f * OUTER_AVG_SUBPHASES_PER_TICK) /
    (TICK_PERIOD_SECONDS * TICK_PERIOD_SECONDS);
const float INNER_MAX_ACCELERATION_SUBPHASES_PER_S2 =
    (4.0f * INNER_AVG_SUBPHASES_PER_TICK) /
    (TICK_PERIOD_SECONDS * TICK_PERIOD_SECONDS);
// One whole phase per carrier cycle would be an absolute upper bound if we
// want to guarantee we never jump across multiple phases in one publish.
const float HARD_MAX_VELOCITY_SUBPHASES_PER_S =
    (SUBPHASES_PER_PHASE * 1000000.0f) / CARRIER_PERIOD_US;

static_assert((1000000UL % SECOND_HAND_TICKS_PER_SECOND) == 0UL,
              "tick rate must divide one second exactly");
static_assert((SUBPHASES_PER_ROTATION % SECOND_HAND_TICKS_PER_ROTATION) == 0UL,
              "outer hand tick size must be integral");

#define INNER_PIN_1 5
#define INNER_PIN_2 4
#define INNER_PIN_3 7
#define INNER_PIN_4 6

#define OUTER_PIN_1 9
#define OUTER_PIN_2 8
#define OUTER_PIN_3 3
#define OUTER_PIN_4 2

const int8_t INNER_DIRECTION = 1;
const int8_t OUTER_DIRECTION = -1;

struct inner_motor_spec : microphase_motor_spec_t<CONTACT_COUNT, PHASE_COUNT> {
  static const uint8_t pins[pin_count];
  static const uint8_t phases[phase_count][pin_count];
};

const uint8_t inner_motor_spec::pins[inner_motor_spec::pin_count] = {
  INNER_PIN_1,
  INNER_PIN_2,
  INNER_PIN_3,
  INNER_PIN_4,
};

const uint8_t inner_motor_spec::phases[inner_motor_spec::phase_count]
                                      [inner_motor_spec::pin_count] = {
  {1, 0, 0, 1},
  {0, 0, 0, 1},
  {0, 1, 1, 1},
  {0, 1, 1, 0},
  {1, 1, 1, 0},
  {1, 0, 0, 0},
};

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

microphase_motor_driver<inner_motor_spec, outer_motor_spec> driver;

struct AxleState {
  uint32_t commandedSubphasePosition;
  uint32_t publishedSubphasePosition;
  float actualSubphasePosition;
  float velocitySubphasesPerSecond;
  float maxAccelerationSubphasesPerSecond2;
  int8_t direction;
};

AxleState innerAxle = {
  0UL,
  0UL,
  0.0f,
  0.0f,
  INNER_MAX_ACCELERATION_SUBPHASES_PER_S2,
  INNER_DIRECTION
};

AxleState outerAxle = {
  0UL,
  0UL,
  0.0f,
  0.0f,
  OUTER_MAX_ACCELERATION_SUBPHASES_PER_S2,
  OUTER_DIRECTION
};

uint32_t nextBeatUs = 0UL;
uint32_t lastMotionUs = 0UL;
uint32_t minuteHandAccumulator = 0UL;

inline uint8_t wrapPhaseIndex(int16_t value) {
  value %= PHASE_COUNT;
  if (value < 0) {
    value += PHASE_COUNT;
  }
  return (uint8_t)value;
}

inline uint8_t phaseFromCount(uint32_t phaseCount, int8_t direction) {
  return wrapPhaseIndex((int16_t)(direction * (int32_t)(phaseCount % PHASE_COUNT)));
}

uint32_t wrapSubphasePosition(uint32_t subphasePosition) {
  return subphasePosition % SUBPHASES_PER_ROTATION;
}

template <uint8_t MotorIndex>
void publishSubphasePosition(int8_t direction, uint32_t subphasePosition) {
  const uint32_t wrappedSubphasePosition = wrapSubphasePosition(subphasePosition);
  const uint32_t phaseCount = wrappedSubphasePosition / SUBPHASES_PER_PHASE;
  const uint8_t phaseA = phaseFromCount(phaseCount, direction);
  const uint8_t phaseB = wrapPhaseIndex((int16_t)phaseA + direction);
  const uint8_t blendValue =
      (uint8_t)(wrappedSubphasePosition % SUBPHASES_PER_PHASE);

  driver.template publish<MotorIndex>(phaseA, phaseB, blendValue);
}

void advanceRolexBeat() {
  outerAxle.commandedSubphasePosition += OUTER_SUBPHASES_PER_TICK;

  minuteHandAccumulator += SUBPHASES_PER_ROTATION;
  const uint32_t innerAdvance =
      minuteHandAccumulator / MINUTE_HAND_TICKS_PER_ROTATION;
  minuteHandAccumulator %= MINUTE_HAND_TICKS_PER_ROTATION;

  innerAxle.commandedSubphasePosition += innerAdvance;
}

void normalizeAxle(AxleState &axle) {
  while (axle.commandedSubphasePosition >= SUBPHASES_PER_ROTATION &&
         axle.publishedSubphasePosition >= SUBPHASES_PER_ROTATION) {
    axle.commandedSubphasePosition -= SUBPHASES_PER_ROTATION;
    axle.publishedSubphasePosition -= SUBPHASES_PER_ROTATION;
    axle.actualSubphasePosition -= (float)SUBPHASES_PER_ROTATION;
    if (axle.actualSubphasePosition < 0.0f) {
      axle.actualSubphasePosition = 0.0f;
    }
  }
}

template <uint8_t MotorIndex>
void serviceAxleMotion(AxleState &axle, float dtSeconds) {
  const float commandedPosition = (float)axle.commandedSubphasePosition;
  float remainingSubphases = commandedPosition - axle.actualSubphasePosition;

  if (remainingSubphases <= 0.0f) {
    axle.actualSubphasePosition = commandedPosition;
    axle.velocitySubphasesPerSecond = 0.0f;
  } else {
    axle.velocitySubphasesPerSecond +=
        axle.maxAccelerationSubphasesPerSecond2 * dtSeconds;
    if (axle.velocitySubphasesPerSecond > HARD_MAX_VELOCITY_SUBPHASES_PER_S) {
      axle.velocitySubphasesPerSecond = HARD_MAX_VELOCITY_SUBPHASES_PER_S;
    }

    axle.actualSubphasePosition += axle.velocitySubphasesPerSecond * dtSeconds;
    if (axle.actualSubphasePosition >= commandedPosition) {
      axle.actualSubphasePosition = commandedPosition;
      axle.velocitySubphasesPerSecond = 0.0f;
    }
  }

  const uint32_t desiredPublishedSubphasePosition =
      (uint32_t)(axle.actualSubphasePosition + 0.5f);

  while (desiredPublishedSubphasePosition > axle.publishedSubphasePosition) {
    uint32_t deltaSubphases =
        desiredPublishedSubphasePosition - axle.publishedSubphasePosition;
    if (deltaSubphases > MAX_PUBLISH_DELTA_SUBPHASES) {
      deltaSubphases = MAX_PUBLISH_DELTA_SUBPHASES;
    }

    axle.publishedSubphasePosition += deltaSubphases;
    publishSubphasePosition<MotorIndex>(axle.direction,
                                        axle.publishedSubphasePosition);
  }
}

void serviceRolexBeat(uint32_t nowUs) {
  if ((int32_t)(nowUs - nextBeatUs) >= 0) {
    nextBeatUs = nowUs + TICK_PERIOD_US;
    advanceRolexBeat();
  }
}

void serviceMotion(uint32_t nowUs) {
  const uint32_t elapsedUs = (uint32_t)(nowUs - lastMotionUs);
  if (elapsedUs == 0UL) {
    return;
  }

  lastMotionUs = nowUs;
  const float dtSeconds = elapsedUs / 1000000.0f;

  serviceAxleMotion<0>(innerAxle, dtSeconds);
  serviceAxleMotion<1>(outerAxle, dtSeconds);

  normalizeAxle(innerAxle);
  normalizeAxle(outerAxle);
}

void onCarrierTick() {
#ifdef CALLBACK_BENCHMARK_PIN
  digitalWriteFast(CALLBACK_BENCHMARK_PIN, HIGH);
#endif

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

  publishSubphasePosition<0>(innerAxle.direction,
                             innerAxle.publishedSubphasePosition);
  publishSubphasePosition<1>(outerAxle.direction,
                             outerAxle.publishedSubphasePosition);

  const uint32_t nowUs = micros();
  nextBeatUs = nowUs + TICK_PERIOD_US;
  lastMotionUs = nowUs;

  Timer1.initialize(CARRIER_PERIOD_US);
  Timer1.attachInterrupt(onCarrierTick);
}

void loop() {
  const uint32_t nowUs = micros();
  serviceRolexBeat(nowUs);
  serviceMotion(nowUs);
}
