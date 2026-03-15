// A demo of a rolex watch face with both hands using subphases from a fixed
// TimerOne carrier. Each axle follows a smooth sine-shaped blend between
// adjacent VID28 phases while still using only the six standard drive states.

// Currently we are only doing 8 subphases per phase (6 phases per step,
// 180 steps per rotation), but this seems to be good enough for these slow
// speeds with my hands.

// The subphases are blended with an 8 entry sine wave table. This still
// sounds and looks smooth enough in practice.

// Currenly the carrier is at ~31Khz to push it above audiable, while still keeping it slow enough
// that we shoud have enough cycles if we switch to a stand-alone AVR chip running off internatyl oscillator. 
// We can (and should) switch to direct PORT writes if we ever run out of horsepower. 


#include <digitalWriteFast.h>
#include <TimerOne.h>

// Uncomment to drive a scope probe high for the duration of the carrier ISR.
// In a perfect world, we woudl write the dithering code in ASM and then count the cycles to make sure we have enough time to do everything.
// But instead (for now) we are using the arduino crap, so is a pain to strictly keep track of how many cycles we are taking. Instead,
// we just measure it emperically by attaching a scope to this pin and watching what percentage of the time we are inside the callback.
// We must also give some allowance for the overhead of TimerOne, but that is written by Paul Stofergen so we know it is good code:).
// Emperically, we are now currently using only ~9us out of ~31us, so pleanty of headroom. 
#define CALLBACK_BENCHMARK_PIN 12

#define PHASE_COUNT 6
#define CONTACT_COUNT 4

// A subphase is one of the evenly spaced target positions between two adjacent
// VID28 phase states. We still only ever drive the real phase states; the ISR
// averages between the current pair quickly enough that the motor sees a
// smoother position in between them.
#define SUBPHASES_PER_PHASE 8

const uint16_t COARSE_PHASES_PER_ROTATION = 360U * 3U;
const uint16_t SUBPHASES_PER_ROTATION =
    COARSE_PHASES_PER_ROTATION * SUBPHASES_PER_PHASE;

const uint32_t OUTER_ROTATION_PERIOD_US = 60000000UL;
const uint32_t INNER_ROTATION_PERIOD_US = OUTER_ROTATION_PERIOD_US * 60UL;
const uint32_t CARRIER_PERIOD_US = 32UL;

// `blendWeight` is on a 0..255 scale. 0 means "always emit basePhase" and
// values near 255 mean "almost always emit nextPhase". Each entry maps one
// subphase inside a coarse phase interval to the desired average on-time of
// nextPhase in the carrier ISR.
const uint8_t blendWeights[SUBPHASES_PER_PHASE] = {
  0, 10, 37, 79, 128, 176, 218, 245
};

// Contacts 2 and 3 are always the same in the VID28 sequence.
const uint8_t phases[PHASE_COUNT][CONTACT_COUNT] = {
  {1,0,0,1},    //1
  {0,0,0,1},    //2
  {0,1,1,1},    //3
  {0,1,1,0},    //4
  {1,1,1,0},    //5
  {1,0,0,0},    //6
};

#define INNER_PIN_1 5
#define INNER_PIN_2 4
#define INNER_PIN_3 7
#define INNER_PIN_4 6

#define OUTER_PIN_1 9
#define OUTER_PIN_2 8
#define OUTER_PIN_3 3
#define OUTER_PIN_4 2

struct AxleState {
  uint32_t rotationPeriodUs;
  int8_t direction;
  uint16_t lastSubphasePosition;
  volatile uint8_t basePhase;
  volatile uint8_t nextPhase;
  volatile uint8_t blendWeight;
  volatile uint8_t blendAccumulator;
};

AxleState outerAxle = {
  OUTER_ROTATION_PERIOD_US,
  -1,
  0,
  0,
  PHASE_COUNT - 1,
  0,
  0
};

AxleState innerAxle = {
  INNER_ROTATION_PERIOD_US,
  1,
  0,
  0,
  1,
  0,
  0
};

uint32_t lastMicrosLow = 0;
uint64_t microsEpoch = 0;
uint64_t startedUs = 0;

inline uint8_t wrapPhaseIndex(int16_t value) {
  value %= PHASE_COUNT;
  if (value < 0) {
    value += PHASE_COUNT;
  }
  return (uint8_t)value;
}

inline uint8_t phaseFromCount(uint16_t phaseCount, int8_t direction) {
  return wrapPhaseIndex(direction * (int16_t)(phaseCount % PHASE_COUNT));
}

inline void writeOuterPhase(uint8_t phaseIndex) {
  if (phases[phaseIndex][0]) { digitalWriteFast(OUTER_PIN_1, HIGH); }
  else { digitalWriteFast(OUTER_PIN_1, LOW); }

  if (phases[phaseIndex][1]) { digitalWriteFast(OUTER_PIN_2, HIGH); }
  else { digitalWriteFast(OUTER_PIN_2, LOW); }

  if (phases[phaseIndex][2]) { digitalWriteFast(OUTER_PIN_3, HIGH); }
  else { digitalWriteFast(OUTER_PIN_3, LOW); }

  if (phases[phaseIndex][3]) { digitalWriteFast(OUTER_PIN_4, HIGH); }
  else { digitalWriteFast(OUTER_PIN_4, LOW); }
}

inline void writeInnerPhase(uint8_t phaseIndex) {
  if (phases[phaseIndex][0]) { digitalWriteFast(INNER_PIN_1, HIGH); }
  else { digitalWriteFast(INNER_PIN_1, LOW); }

  if (phases[phaseIndex][1]) { digitalWriteFast(INNER_PIN_2, HIGH); }
  else { digitalWriteFast(INNER_PIN_2, LOW); }

  if (phases[phaseIndex][2]) { digitalWriteFast(INNER_PIN_3, HIGH); }
  else { digitalWriteFast(INNER_PIN_3, LOW); }

  if (phases[phaseIndex][3]) { digitalWriteFast(INNER_PIN_4, HIGH); }
  else { digitalWriteFast(INNER_PIN_4, LOW); }
}

uint64_t micros64() {
  const uint32_t nowLow = micros();

  // the micros will overflow an int32 in like an hour, so we expand into a 64 bit value
  // by counting the number of overflows. This should be good for like 500k years, so we won't have to worry about it rolling over again.
  if (nowLow < lastMicrosLow) {
    microsEpoch += (1ULL << 32);
  }

  lastMicrosLow = nowLow;
  return microsEpoch + nowLow;
}

// A waste to recomute this from scratch everytime (espcially with all those uint64s on an 8bit micro!), 
// but it's only 18000 times per rotation and we have nothing better to do between callbacks anyway. And 
// this approach makes it easy for us to change the rotation pattern while still using this framework. 
uint16_t positionInRotation(uint64_t elapsedUs,
                            uint32_t rotationPeriodUs,
                            uint16_t positionsPerRotation) {
  const uint64_t elapsedInRotation = elapsedUs % rotationPeriodUs;
  return (uint16_t)((elapsedInRotation * positionsPerRotation) / rotationPeriodUs);
}

// Note that we can not just jump around - positions must be continuous and we
// can not jump faster than we can turn the motor.
void publishSubphase(AxleState &axle, uint16_t subphasePosition) {
  const uint16_t phaseCount = subphasePosition / SUBPHASES_PER_PHASE;
  const uint8_t subphase = subphasePosition % SUBPHASES_PER_PHASE;
  const uint8_t basePhase = phaseFromCount(phaseCount, axle.direction);
  const uint8_t nextPhase = wrapPhaseIndex((int16_t)basePhase + axle.direction);

  noInterrupts();
  axle.basePhase = basePhase;
  axle.nextPhase = nextPhase;
  axle.blendWeight = blendWeights[subphase];
  interrupts();
}

void updateAxleTarget(AxleState &axle, uint64_t elapsedUs) {
  const uint16_t subphasePosition = positionInRotation(
      elapsedUs, axle.rotationPeriodUs, SUBPHASES_PER_ROTATION);

  if (subphasePosition != axle.lastSubphasePosition) {
    axle.lastSubphasePosition = subphasePosition;
    publishSubphase(axle, subphasePosition);
  }
}

inline uint8_t blendedPhase(AxleState &axle) {
  uint8_t phaseIndex = axle.basePhase;
  const uint8_t blendWeight = axle.blendWeight;

  if (blendWeight != 0) {
    const uint16_t nextAccumulator =
        (uint16_t)axle.blendAccumulator + blendWeight;

    if (nextAccumulator >= 256) {
      axle.blendAccumulator = (uint8_t)(nextAccumulator - 256);
      phaseIndex = axle.nextPhase;
    } else {
      axle.blendAccumulator = (uint8_t)nextAccumulator;
    }
  } else {
    axle.blendAccumulator = 0;
  }

  return phaseIndex;
}

void onCarrierTick() {
#ifdef CALLBACK_BENCHMARK_PIN
  digitalWriteFast(CALLBACK_BENCHMARK_PIN, HIGH);
#endif

  writeInnerPhase(blendedPhase(innerAxle));
  writeOuterPhase(blendedPhase(outerAxle));

#ifdef CALLBACK_BENCHMARK_PIN
  digitalWriteFast(CALLBACK_BENCHMARK_PIN, LOW);
#endif
}

void setup() {
  pinMode(INNER_PIN_1, OUTPUT);
  pinMode(INNER_PIN_2, OUTPUT);
  pinMode(INNER_PIN_3, OUTPUT);
  pinMode(INNER_PIN_4, OUTPUT);

  pinMode(OUTER_PIN_1, OUTPUT);
  pinMode(OUTER_PIN_2, OUTPUT);
  pinMode(OUTER_PIN_3, OUTPUT);
  pinMode(OUTER_PIN_4, OUTPUT);

#ifdef CALLBACK_BENCHMARK_PIN
  pinMode(CALLBACK_BENCHMARK_PIN, OUTPUT);
  digitalWriteFast(CALLBACK_BENCHMARK_PIN, LOW);
#endif

  const uint32_t startLow = micros();
  lastMicrosLow = startLow;
  startedUs = (uint64_t)startLow;

  publishSubphase(innerAxle, 0);
  publishSubphase(outerAxle, 0);
  writeInnerPhase(innerAxle.basePhase);
  writeOuterPhase(outerAxle.basePhase);

  Timer1.initialize(CARRIER_PERIOD_US);
  Timer1.attachInterrupt(onCarrierTick);
}

void loop() {
  const uint64_t elapsedUs = micros64() - startedUs;
  updateAxleTarget(innerAxle, elapsedUs);
  updateAxleTarget(outerAxle, elapsedUs);
}
