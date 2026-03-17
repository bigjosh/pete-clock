// Portable serial-console harness for manually exercising the VID28
// microphase driver on both axles using digitalWriteFast.

#include <Arduino.h>
#include <TimerOne.h>

#include "microphase_motor_driver.h"

// Uncomment to toggle a scope pin at ISR entry/exit.
// #define CALLBACK_BENCHMARK_PIN 12

const uint8_t PHASE_COUNT = 6;
const uint8_t SUBPHASES_PER_PHASE = 8;

const uint32_t SERIAL_BAUD = 9600;
const uint32_t DEFAULT_CARRIER_PERIOD_US = 32UL;
const uint32_t DEFAULT_STEP_PHASE_DELAY_US = 2000UL;
const uint32_t MIN_CARRIER_PERIOD_US = 16UL;
const uint32_t MAX_CARRIER_PERIOD_US = 200UL;
const uint32_t BENCHMARK_DURATION_MS = 2000UL;
const uint32_t SWEEP_ARGS_TIMEOUT_MS = 100UL;
const char DEFAULT_SWEEP_MOTOR = 'o';
const int32_t DEFAULT_SWEEP_VELOCITY_SUBPHASES_PER_SECOND = 20;
const uint32_t DEFAULT_SWEEP_DURATION_MS = 1000UL;
const uint8_t BENCHMARK_PHASE_A = 1;
const uint8_t BENCHMARK_PHASE_B = 2;
const uint8_t BENCHMARK_BLEND_VALUE = 128;
const uint8_t BENCHMARK_BATCH_SIZE = 64;

const uint8_t blendWeights[SUBPHASES_PER_PHASE] = {
  0, 10, 37, 79, 128, 176, 218, 245
};

const int8_t INNER_CW_DIRECTION = 1;
const int8_t OUTER_CW_DIRECTION = -1;

struct inner_motor_spec : microphase_motor_spec_t<4, PHASE_COUNT> {
  static const uint8_t pins[pin_count];
  static const uint8_t phases[phase_count][pin_count];
};

const uint8_t inner_motor_spec::pins[inner_motor_spec::pin_count] = {5, 4, 7, 6};

const uint8_t inner_motor_spec::phases[inner_motor_spec::phase_count]
                                       [inner_motor_spec::pin_count] = {
  {1, 0, 0, 1},
  {0, 0, 0, 1},
  {0, 1, 1, 1},
  {0, 1, 1, 0},
  {1, 1, 1, 0},
  {1, 0, 0, 0},
};

struct outer_motor_spec : microphase_motor_spec_t<4, PHASE_COUNT> {
  static const uint8_t pins[pin_count];
  static const uint8_t phases[phase_count][pin_count];
};

const uint8_t outer_motor_spec::pins[outer_motor_spec::pin_count] = {9, 8, 3, 2};

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

template <uint8_t MotorIndex>
struct AxleState {
  int32_t currentSubphasePosition;
  int8_t cwDirection;
};

AxleState<0> innerAxle = {0L, INNER_CW_DIRECTION};
AxleState<1> outerAxle = {0L, OUTER_CW_DIRECTION};

uint32_t carrierPeriodUs = DEFAULT_CARRIER_PERIOD_US;
uint32_t stepPhaseDelayUs = DEFAULT_STEP_PHASE_DELAY_US;
volatile uint32_t benchmarkSink = 0;

void printPadding(uint8_t count) {
  for (uint8_t index = 0; index < count; ++index) {
    Serial.print(' ');
  }
}

void printAlignedLong(int32_t value, uint8_t width) {
  char buffer[16];
  ltoa(value, buffer, 10);
  const uint8_t length = (uint8_t)strlen(buffer);

  if (length < width) {
    printPadding(width - length);
  }

  Serial.print(buffer);
}

void printAlignedUnsigned(uint32_t value, uint8_t width) {
  char buffer[16];
  ultoa(value, buffer, 10);
  const uint8_t length = (uint8_t)strlen(buffer);

  if (length < width) {
    printPadding(width - length);
  }

  Serial.print(buffer);
}

void printAlignedFloat(float value, uint8_t width, uint8_t precision) {
  char buffer[20];
  dtostrf(value, width, precision, buffer);
  Serial.print(buffer);
}

inline uint8_t wrapPhaseIndex(int16_t value) {
  value %= PHASE_COUNT;
  if (value < 0) {
    value += PHASE_COUNT;
  }
  return (uint8_t)value;
}

inline uint8_t phaseFromCount(int32_t phaseCount, int8_t cwDirection) {
  return wrapPhaseIndex((int16_t)(cwDirection * (phaseCount % PHASE_COUNT)));
}

void decomposeSubphasePosition(int32_t subphasePosition,
                               int32_t &phaseCount,
                               uint8_t &subphase) {
  phaseCount = subphasePosition / SUBPHASES_PER_PHASE;
  int32_t subphaseValue = subphasePosition % SUBPHASES_PER_PHASE;

  if (subphaseValue < 0) {
    subphaseValue += SUBPHASES_PER_PHASE;
    --phaseCount;
  }

  subphase = (uint8_t)subphaseValue;
}

template <uint8_t MotorIndex>
void publishSubphase(AxleState<MotorIndex> &axle, int32_t subphasePosition) {
  int32_t phaseCount = 0;
  uint8_t subphase = 0;
  decomposeSubphasePosition(subphasePosition, phaseCount, subphase);

  const uint8_t phaseA = phaseFromCount(phaseCount, axle.cwDirection);
  const uint8_t phaseB = wrapPhaseIndex((int16_t)phaseA + axle.cwDirection);

  driver.template publish<MotorIndex>(phaseA, phaseB, blendWeights[subphase]);
}

void applyCarrierPeriod() {
  Timer1.initialize(carrierPeriodUs);
  Timer1.attachInterrupt(onCarrierTick);
}

void disableCarrier() {
  Timer1.detachInterrupt();
}

void waitStepPhaseDelay() {
  delay(stepPhaseDelayUs / 1000UL);
  delayMicroseconds(stepPhaseDelayUs % 1000UL);
}

template <uint8_t MotorIndex>
void moveBySubphases(AxleState<MotorIndex> &axle, int32_t deltaSubphases) {
  axle.currentSubphasePosition += deltaSubphases;
  publishSubphase(axle, axle.currentSubphasePosition);
}

template <uint8_t MotorIndex>
void moveByPhases(AxleState<MotorIndex> &axle,
                  int8_t direction,
                  uint8_t phaseCount) {
  const int32_t deltaSubphases = (int32_t)direction * SUBPHASES_PER_PHASE;

  for (uint8_t phase = 0; phase < phaseCount; ++phase) {
    moveBySubphases(axle, deltaSubphases);
    if (phase + 1 < phaseCount) {
      waitStepPhaseDelay();
    }
  }
}

template <uint8_t MotorIndex>
void printAxleStatus(const __FlashStringHelper *label,
                     const AxleState<MotorIndex> &axle) {
  int32_t phaseCount = 0;
  uint8_t subphase = 0;
  decomposeSubphasePosition(axle.currentSubphasePosition, phaseCount, subphase);
  const float angleDeg =
      axle.currentSubphasePosition * 360.0f /
      (PHASE_COUNT * 180.0f * SUBPHASES_PER_PHASE);

  Serial.print(label);
  Serial.print(F(" sub="));
  printAlignedLong(axle.currentSubphasePosition, 7);
  Serial.print(F(" phase="));
  printAlignedLong(phaseCount, 6);
  Serial.print(F(" fine="));
  printAlignedUnsigned(subphase, 2);
  Serial.print(F(" angleDeg="));
  printAlignedFloat(angleDeg, 8, 3);
}

void printStatus() {
  Serial.print(F("carrier="));
  printAlignedFloat(1000000.0f / carrierPeriodUs, 8, 2);
  Serial.print(F("Hz periodUs="));
  printAlignedUnsigned(carrierPeriodUs, 3);
  Serial.print(F(" stepPhaseDelayUs="));
  printAlignedUnsigned(stepPhaseDelayUs, 5);
  Serial.print(F(" | "));
  printAxleStatus(F("inner"), innerAxle);
  Serial.print(F(" | "));
  printAxleStatus(F("outer"), outerAxle);
  Serial.println();
}

void printHelp() {
  Serial.println(F("test-microphase-portable controls"));
  Serial.println(F("Q/W/E = inner CCW step / phase / subphase"));
  Serial.println(F("R/T/Y = inner CW subphase / phase / step"));
  Serial.println(F("S [m [v [t]]] = sweep, defaults: o 20 1000"));
  Serial.println(F("Z/X/C = outer CCW step / phase / subphase"));
  Serial.println(F("V/B/N = outer CW subphase / phase / step"));
  Serial.println(F("M     = benchmark average carrier CPU burden"));
  Serial.println(F("-/=   = carrier slower / faster (period +/- 1 us)"));
  Serial.println(F("[/]   = step phase delay -/+ 100 us"));
  Serial.println(F("P     = print status"));
  Serial.println(F("H/?   = help"));
  printStatus();
}

__attribute__((noinline)) uint32_t runBenchmarkLoop(uint32_t durationMs) {
  const uint32_t startMs = millis();
  uint32_t iterations = 0;
  uint32_t state = 1;

  while ((uint32_t)(millis() - startMs) < durationMs) {
    for (uint8_t batch = 0; batch < BENCHMARK_BATCH_SIZE; ++batch) {
      state = state * 33UL + 17UL;
      ++iterations;
    }
  }

  benchmarkSink = state;
  return iterations;
}

void runCarrierBurdenBenchmark() {
  const int32_t savedInnerSubphase = innerAxle.currentSubphasePosition;
  const int32_t savedOuterSubphase = outerAxle.currentSubphasePosition;

  Serial.println(F("benchmarking carrier burden..."));
  Serial.flush();

  driver.template publish<0>(BENCHMARK_PHASE_A, BENCHMARK_PHASE_B,
                             BENCHMARK_BLEND_VALUE);
  driver.template publish<1>(BENCHMARK_PHASE_A, BENCHMARK_PHASE_B,
                             BENCHMARK_BLEND_VALUE);

  disableCarrier();
  const uint32_t iterationsOff = runBenchmarkLoop(BENCHMARK_DURATION_MS);

  applyCarrierPeriod();
  const uint32_t iterationsOn = runBenchmarkLoop(BENCHMARK_DURATION_MS);

  disableCarrier();
  publishSubphase(innerAxle, savedInnerSubphase);
  publishSubphase(outerAxle, savedOuterSubphase);
  applyCarrierPeriod();

  if (iterationsOff == 0) {
    Serial.println(F("benchmark failed: zero baseline iterations"));
    return;
  }

  const float foregroundRatio = (float)iterationsOn / (float)iterationsOff;
  const float burdenRatio = 1.0f - foregroundRatio;
  const float burdenPct = burdenRatio * 100.0f;
  const float carrierHz = 1000000.0f / (float)carrierPeriodUs;
  const float avgCyclesPerTick =
      burdenRatio * (float)F_CPU / carrierHz;

  Serial.print(F("benchmark offIters="));
  printAlignedUnsigned(iterationsOff, 10);
  Serial.print(F(" onIters="));
  printAlignedUnsigned(iterationsOn, 10);
  Serial.print(F(" foreground="));
  printAlignedFloat(foregroundRatio * 100.0f, 7, 2);
  Serial.print(F("% burden="));
  printAlignedFloat(burdenPct, 7, 2);
  Serial.print(F("% avgCyclesPerTick="));
  printAlignedFloat(avgCyclesPerTick, 8, 1);
  Serial.println();

  printStatus();
}

char *skipSpaces(char *cursor) {
  while (*cursor == ' ' || *cursor == '\t') {
    ++cursor;
  }
  return cursor;
}

bool readPendingCommandArgs(char *buffer, size_t capacity, uint32_t timeoutMs) {
  size_t length = 0;
  uint32_t lastByteMs = millis();

  while ((uint32_t)(millis() - lastByteMs) < timeoutMs) {
    while (Serial.available() > 0) {
      const char input = (char)Serial.read();
      lastByteMs = millis();

      if (input == '\r') {
        continue;
      }

      if (input == '\n') {
        buffer[length] = '\0';
        return true;
      }

      if (length + 1 < capacity) {
        buffer[length++] = input;
      }
    }
  }

  buffer[length] = '\0';
  return true;
}

int32_t scaledSubphaseDelta(int32_t velocitySubphasesPerSecond,
                            uint32_t elapsedUs) {
  const int64_t numerator =
      (int64_t)velocitySubphasesPerSecond * (int64_t)elapsedUs;

  if (numerator >= 0) {
    return (int32_t)(numerator / 1000000LL);
  }

  return (int32_t)(-(((-numerator) + 999999LL) / 1000000LL));
}

template <uint8_t MotorIndex>
void sweepAxle(AxleState<MotorIndex> &axle,
               int32_t velocitySubphasesPerSecond,
               uint32_t durationMs) {
  const int32_t startSubphasePosition = axle.currentSubphasePosition;
  const uint32_t durationUs = durationMs * 1000UL;
  const uint32_t startUs = micros();

  while (true) {
    const uint32_t elapsedUs = (uint32_t)(micros() - startUs);
    const uint32_t clampedElapsedUs =
        (elapsedUs < durationUs) ? elapsedUs : durationUs;
    const int32_t targetSubphasePosition =
        startSubphasePosition +
        scaledSubphaseDelta(velocitySubphasesPerSecond, clampedElapsedUs);

    if (targetSubphasePosition != axle.currentSubphasePosition) {
      axle.currentSubphasePosition = targetSubphasePosition;
      publishSubphase(axle, axle.currentSubphasePosition);
    }

    if (elapsedUs >= durationUs) {
      break;
    }
  }

  const int32_t finalSubphasePosition =
      startSubphasePosition +
      scaledSubphaseDelta(velocitySubphasesPerSecond, durationUs);
  axle.currentSubphasePosition = finalSubphasePosition;
  publishSubphase(axle, axle.currentSubphasePosition);
}

void runSweepCommand() {
  char line[48];
  readPendingCommandArgs(line, sizeof(line), SWEEP_ARGS_TIMEOUT_MS);

  char motorName = DEFAULT_SWEEP_MOTOR;
  char *cursor = skipSpaces(line);
  int32_t velocityValue = DEFAULT_SWEEP_VELOCITY_SUBPHASES_PER_SECOND;
  uint32_t durationValue = DEFAULT_SWEEP_DURATION_MS;

  if (motorName >= 'A' && motorName <= 'Z') {
    motorName = (char)(motorName - 'A' + 'a');
  }

  if (*cursor == 'i' || *cursor == 'I' || *cursor == 'o' || *cursor == 'O') {
    motorName = (char)(*cursor - ((*cursor >= 'A' && *cursor <= 'Z') ? 'A' - 'a' : 0));
    cursor = skipSpaces(cursor + 1);
  }

  if (*cursor != '\0') {
    char *parseEnd = cursor;
    velocityValue = (int32_t)strtol(cursor, &parseEnd, 10);
    if (parseEnd == cursor) {
      Serial.println(F("sweep format error"));
      return;
    }
    cursor = skipSpaces(parseEnd);
  }

  if (*cursor != '\0') {
    char *parseEnd = cursor;
    durationValue = (uint32_t)strtoul(cursor, &parseEnd, 10);
    if (parseEnd == cursor) {
      Serial.println(F("sweep format error"));
      return;
    }
    cursor = skipSpaces(parseEnd);
  }

  if (*cursor != '\0') {
    Serial.println(F("sweep format error"));
    return;
  }

  Serial.print(F("sweeping motor="));
  Serial.print(motorName);
  Serial.print(F(" velocity="));
  Serial.print(velocityValue);
  Serial.print(F(" subphases/s durationMs="));
  Serial.println(durationValue);
  Serial.flush();

  switch (motorName) {
    case 'i':
      sweepAxle(innerAxle, (int32_t)velocityValue, (uint32_t)durationValue);
      break;
    case 'o':
      sweepAxle(outerAxle, (int32_t)velocityValue, (uint32_t)durationValue);
      break;
    default:
      Serial.println(F("sweep motor must be 'i' or 'o'"));
      return;
  }

  printStatus();
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

void handleCommand(char command) {
  switch (command) {
    case 'q':
      moveByPhases(innerAxle, -1, PHASE_COUNT);
      break;
    case 'w':
      moveByPhases(innerAxle, -1, 1);
      break;
    case 'e':
      moveBySubphases(innerAxle, -1);
      break;
    case 'r':
      moveBySubphases(innerAxle, 1);
      break;
    case 't':
      moveByPhases(innerAxle, 1, 1);
      break;
    case 'y':
      moveByPhases(innerAxle, 1, PHASE_COUNT);
      break;
    case 'z':
      moveByPhases(outerAxle, -1, PHASE_COUNT);
      break;
    case 'x':
      moveByPhases(outerAxle, -1, 1);
      break;
    case 'c':
      moveBySubphases(outerAxle, -1);
      break;
    case 'v':
      moveBySubphases(outerAxle, 1);
      break;
    case 'b':
      moveByPhases(outerAxle, 1, 1);
      break;
    case 'n':
      moveByPhases(outerAxle, 1, PHASE_COUNT);
      break;
    case 's':
      runSweepCommand();
      return;
    case 'm':
      runCarrierBurdenBenchmark();
      return;
    case '-':
      if (carrierPeriodUs < MAX_CARRIER_PERIOD_US) {
        ++carrierPeriodUs;
        applyCarrierPeriod();
      }
      break;
    case '=':
      if (carrierPeriodUs > MIN_CARRIER_PERIOD_US) {
        --carrierPeriodUs;
        applyCarrierPeriod();
      }
      break;
    case '[':
      if (stepPhaseDelayUs >= 100UL) {
        stepPhaseDelayUs -= 100UL;
      }
      break;
    case ']':
      stepPhaseDelayUs += 100UL;
      break;
    case 'p':
      printStatus();
      return;
    case 'h':
    case '?':
      printHelp();
      return;
    case '\r':
    case '\n':
      return;
    default:
      Serial.print(F("unknown key: "));
      Serial.println(command);
      printHelp();
      return;
  }

  printStatus();
}

void setup() {
  driver.begin();

#ifdef CALLBACK_BENCHMARK_PIN
  pinModeFast(CALLBACK_BENCHMARK_PIN, OUTPUT);
  digitalWriteFast(CALLBACK_BENCHMARK_PIN, LOW);
#endif

  Serial.begin(SERIAL_BAUD);

  publishSubphase(innerAxle, innerAxle.currentSubphasePosition);
  publishSubphase(outerAxle, outerAxle.currentSubphasePosition);
  applyCarrierPeriod();
  printHelp();
}

void loop() {
  while (Serial.available() > 0) {
    char command = (char)Serial.read();
    if (command >= 'A' && command <= 'Z') {
      command = (char)(command - 'A' + 'a');
    }
    handleCommand(command);
  }
}
