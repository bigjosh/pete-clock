// Portable serial-console harness for manually exercising the VID28
// microphase driver on both axles using digitalWriteFast.

#include <Arduino.h>
#include <TimerOne.h>

#include "microphase_motor_driver.h"

// Uncomment to toggle a scope pin at ISR entry/exit.
// #define CALLBACK_BENCHMARK_PIN 12

const uint8_t PHASE_COUNT = 6;
const uint16_t SUBPHASES_PER_PHASE = 256U;
static_assert(SUBPHASES_PER_PHASE == 256U,
              "portable microphase tool assumes 256 subphases");
const uint8_t LAST_PHASE = PHASE_COUNT - 1U;
const uint8_t LAST_SUBPHASE = 255U;
static_assert((F_CPU % 1000000UL) == 0UL,
              "integer benchmark formatting expects whole cycles/us");
const uint8_t CYCLES_PER_MICROSECOND = (uint8_t)(F_CPU / 1000000UL);

const uint32_t SERIAL_BAUD = 9600;
const uint8_t DEFAULT_CARRIER_PERIOD_US = 32U;
const uint16_t DEFAULT_MOVE_GAP_US = 2000U;
const uint8_t MIN_CARRIER_PERIOD_US = 16U;
const uint8_t MAX_CARRIER_PERIOD_US = 200U;
const uint32_t COMMAND_ARGS_TIMEOUT_MS = 100UL;
const uint16_t BENCHMARK_DURATION_MS = 2000U;
const uint8_t BENCHMARK_BATCH_SIZE = 64;

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

// const uint8_t outer_motor_spec::pins[outer_motor_spec::pin_count] = {9, 8, 3, 2};
const uint8_t outer_motor_spec::pins[outer_motor_spec::pin_count] = {A0, A1, A2,
                                                                     A3};

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
  uint8_t phase;
  uint8_t subphase;
};

AxleState<0> innerAxle = {0, 0};
AxleState<1> outerAxle = {0, 0};

uint8_t carrierPeriodUs = DEFAULT_CARRIER_PERIOD_US;
uint16_t moveGapUs = DEFAULT_MOVE_GAP_US;
volatile uint32_t benchmarkSink = 0;

char *skipSpaces(char *cursor) {
  while (*cursor == ' ' || *cursor == '\t') {
    ++cursor;
  }
  return cursor;
}

char normalizeLowerChar(char value) {
  if (value >= 'A' && value <= 'Z') {
    return (char)(value - 'A' + 'a');
  }

  return value;
}

inline uint8_t nextPhaseIndex(uint8_t phase) {
  return (phase == LAST_PHASE) ? 0U : (uint8_t)(phase + 1U);
}

inline uint8_t prevPhaseIndex(uint8_t phase) {
  return (phase == 0U) ? LAST_PHASE : (uint8_t)(phase - 1U);
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

bool parseOptionalUint32Token(char *&cursor, uint32_t &value) {
  cursor = skipSpaces(cursor);
  if (*cursor == '\0') {
    return true;
  }

  if (*cursor == '+' || *cursor == '-') {
    return false;
  }

  char *parseEnd = cursor;
  const unsigned long parsedValue = strtoul(cursor, &parseEnd, 10);
  if (parseEnd == cursor) {
    return false;
  }

  value = (uint32_t)parsedValue;
  cursor = skipSpaces(parseEnd);
  return true;
}

bool parseRequiredUint32Token(char *&cursor, uint32_t &value) {
  cursor = skipSpaces(cursor);
  if (*cursor == '\0') {
    return false;
  }

  return parseOptionalUint32Token(cursor, value);
}

bool parseMotorNameToken(char *&cursor, char &motorName) {
  cursor = skipSpaces(cursor);
  if (*cursor == '\0') {
    return false;
  }

  const char normalized = normalizeLowerChar(*cursor);
  if (normalized != 'i' && normalized != 'o') {
    return false;
  }

  motorName = normalized;
  cursor = skipSpaces(cursor + 1);
  return true;
}

template <uint8_t MotorIndex>
void publishAxle(const AxleState<MotorIndex> &axle) {
  const uint8_t phaseA = axle.phase;
  const uint8_t phaseB = nextPhaseIndex(phaseA);

  driver.template publish<MotorIndex>(phaseA, phaseB, axle.subphase);
}

template <uint8_t MotorIndex>
void setAxleState(AxleState<MotorIndex> &axle,
                  uint8_t phase,
                  uint8_t subphase) {
  axle.phase = phase;
  axle.subphase = subphase;
  publishAxle(axle);
}

void applyCarrierPeriod() {
  Timer1.initialize(carrierPeriodUs);
  Timer1.attachInterrupt(onCarrierTick);
}

void disableCarrier() {
  Timer1.detachInterrupt();
}

void waitDelayUs(uint16_t delayUs) {
  delay(delayUs / 1000UL);
  delayMicroseconds(delayUs % 1000UL);
}

template <uint8_t MotorIndex>
void stepPhaseUp(AxleState<MotorIndex> &axle) {
  axle.phase = nextPhaseIndex(axle.phase);
  axle.subphase = 0U;
  publishAxle(axle);
}

template <uint8_t MotorIndex>
void stepPhaseDown(AxleState<MotorIndex> &axle) {
  if (axle.subphase == 0U) {
    axle.phase = prevPhaseIndex(axle.phase);
  }
  axle.subphase = 0U;
  publishAxle(axle);
}

template <uint8_t MotorIndex>
void stepSubphaseUp(AxleState<MotorIndex> &axle) {
  const uint8_t nextSubphase = (uint8_t)(axle.subphase + 1U);
  axle.subphase = nextSubphase;
  if (nextSubphase == 0U) {
    axle.phase = nextPhaseIndex(axle.phase);
  }
  publishAxle(axle);
}

template <uint8_t MotorIndex>
void stepSubphaseDown(AxleState<MotorIndex> &axle) {
  if (axle.subphase == 0U) {
    axle.subphase = LAST_SUBPHASE;
    axle.phase = prevPhaseIndex(axle.phase);
  } else {
    --axle.subphase;
  }
  publishAxle(axle);
}

bool parseMovementArgs(uint16_t &repeatCount, uint16_t &repeatGapUs) {
  char line[32];
  readPendingCommandArgs(line, sizeof(line), COMMAND_ARGS_TIMEOUT_MS);

  char *cursor = skipSpaces(line);
  uint32_t parsedRepeatCount = 1UL;
  uint32_t parsedRepeatGapUs = moveGapUs;

  if (!parseOptionalUint32Token(cursor, parsedRepeatCount) ||
      !parseOptionalUint32Token(cursor, parsedRepeatGapUs) ||
      *skipSpaces(cursor) != '\0') {
    Serial.println(F("move format error"));
    return false;
  }

  if (parsedRepeatCount == 0UL) {
    Serial.println(F("repeat count must be >= 1"));
    return false;
  }

  if (parsedRepeatCount > 65535UL || parsedRepeatGapUs > 65535UL) {
    Serial.println(F("move args too large"));
    return false;
  }

  repeatCount = (uint16_t)parsedRepeatCount;
  repeatGapUs = (uint16_t)parsedRepeatGapUs;
  return true;
}

void printFixed2(uint32_t valueTimes100) {
  Serial.print(valueTimes100 / 100U);
  Serial.print('.');
  const uint8_t fractional = (uint8_t)(valueTimes100 % 100U);
  if (fractional < 10U) {
    Serial.print('0');
  }
  Serial.print(fractional);
}

void printFixed1(uint32_t valueTimes10) {
  Serial.print(valueTimes10 / 10U);
  Serial.print('.');
  Serial.print((uint8_t)(valueTimes10 % 10U));
}

uint16_t ratioPercentTimes100(uint32_t numerator, uint32_t denominator) {
  const uint32_t scaledWholePercent = (numerator * 100U) / denominator;
  const uint32_t scaledRemainder = (numerator * 100U) % denominator;
  uint32_t hundredths =
      (scaledRemainder * 100U + (denominator / 2U)) / denominator;
  uint32_t result = scaledWholePercent * 100U;

  if (hundredths >= 100U) {
    hundredths -= 100U;
    result += 100U;
  }

  result += hundredths;
  if (result > 10000U) {
    result = 10000U;
  }

  return (uint16_t)result;
}

template <uint8_t MotorIndex>
bool runMovementCommand(AxleState<MotorIndex> &axle,
                        bool moveUp,
                        bool moveWholePhases) {
  uint16_t repeatCount = 1U;
  uint16_t repeatGapUs = moveGapUs;

  if (!parseMovementArgs(repeatCount, repeatGapUs)) {
    return false;
  }

  while (true) {
    if (moveWholePhases) {
      if (moveUp) {
        stepPhaseUp(axle);
      } else {
        stepPhaseDown(axle);
      }
    } else if (moveUp) {
      stepSubphaseUp(axle);
    } else {
      stepSubphaseDown(axle);
    }

    --repeatCount;
    if (repeatCount == 0U) {
      break;
    }

    waitDelayUs(repeatGapUs);
  }

  return true;
}

template <uint8_t MotorIndex>
void printAxleStatus(const __FlashStringHelper *label,
                     const AxleState<MotorIndex> &axle) {
  Serial.print(label);
  Serial.print(F(" phase="));
  Serial.print(axle.phase);
  Serial.print(F(" subphase="));
  Serial.print(axle.subphase);
}

void printStatus() {
  const uint32_t carrierHzTimes100 =
      (100000000UL + (carrierPeriodUs / 2U)) / carrierPeriodUs;

  Serial.print(F("carrier="));
  printFixed2(carrierHzTimes100);
  Serial.print(F("Hz periodUs="));
  Serial.print(carrierPeriodUs);
  Serial.print(F(" moveGapUs="));
  Serial.print(moveGapUs);
  Serial.print(F(" | "));
  printAxleStatus(F("inner"), innerAxle);
  Serial.print(F(" | "));
  printAxleStatus(F("outer"), outerAxle);
  Serial.println();
}

void printHelp() {
  Serial.println(F("subphase-bench-portable controls"));
  Serial.println(F("INNER AXLE MOVES: Q=CCW phase W=CCW subphase - E=CW subphase  R=CW phase"));
  Serial.println(F("OUTER AXLE MOVES: A=CCW phase S=CCW subphase - D=CW subphase  F=CW phase"));
  Serial.println(F("Phase moves snap to the next whole phase and set subphase=0"));
  Serial.println(F("Movement keys accept [count [gapUs]]"));
  Serial.println(F("G [i|o] [phase] [subphase] = set motor directly"));
  Serial.println(F("M     = benchmark average carrier CPU burden"));
  Serial.println(F("-/=   = carrier slower / faster (period +/- 1 us)"));
  Serial.println(F("[/]   = move gap -/+ 100 us"));
  Serial.println(F("P     = print status"));
  Serial.println(F("H/?   = help"));
  printStatus();
}

__attribute__((noinline)) uint32_t runBenchmarkLoop(uint16_t durationMs) {
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
  Serial.println(F("benchmarking carrier burden..."));
  Serial.flush();

  disableCarrier();
  const uint32_t iterationsOff = runBenchmarkLoop(BENCHMARK_DURATION_MS);

  applyCarrierPeriod();
  const uint32_t iterationsOn = runBenchmarkLoop(BENCHMARK_DURATION_MS);

  disableCarrier();
  applyCarrierPeriod();

  if (iterationsOff == 0) {
    Serial.println(F("benchmark failed: zero baseline iterations"));
    return;
  }

  const uint16_t foregroundPctTimes100 =
      ratioPercentTimes100(iterationsOn, iterationsOff);
  const uint16_t burdenPctTimes100 =
      (uint16_t)(10000U - foregroundPctTimes100);
  const uint32_t avgCyclesPerTickTimes10 =
      ((uint32_t)burdenPctTimes100 * (uint32_t)carrierPeriodUs *
       (uint32_t)CYCLES_PER_MICROSECOND * 10U + 5000U) /
      10000U;

  Serial.print(F("benchmark offIters="));
  Serial.print(iterationsOff);
  Serial.print(F(" onIters="));
  Serial.print(iterationsOn);
  Serial.print(F(" foreground="));
  printFixed2(foregroundPctTimes100);
  Serial.print(F("% burden="));
  printFixed2(burdenPctTimes100);
  Serial.print(F("% avgCyclesPerTick="));
  printFixed1(avgCyclesPerTickTimes10);
  Serial.println();

  printStatus();
}

void runSetCommand() {
  char line[32];
  readPendingCommandArgs(line, sizeof(line), COMMAND_ARGS_TIMEOUT_MS);

  char *cursor = skipSpaces(line);
  char motorName = '\0';
  uint32_t phaseValue = 0UL;
  uint32_t subphaseValue = 0UL;

  if (!parseMotorNameToken(cursor, motorName) ||
      !parseRequiredUint32Token(cursor, phaseValue) ||
      !parseRequiredUint32Token(cursor, subphaseValue) ||
      *skipSpaces(cursor) != '\0') {
    Serial.println(F("set format: G [i|o] [phase] [subphase]"));
    return;
  }

  if (phaseValue > LAST_PHASE || subphaseValue > LAST_SUBPHASE) {
    Serial.println(F("set range: phase 0-5, subphase 0-255"));
    return;
  }

  switch (motorName) {
    case 'i':
      setAxleState(innerAxle, (uint8_t)phaseValue, (uint8_t)subphaseValue);
      break;
    case 'o':
      setAxleState(outerAxle, (uint8_t)phaseValue, (uint8_t)subphaseValue);
      break;
  }
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
      if (!runMovementCommand(innerAxle, false, true)) {
        return;
      }
      break;
    case 'w':
      if (!runMovementCommand(innerAxle, false, false)) {
        return;
      }
      break;
    case 'e':
      if (!runMovementCommand(innerAxle, true, false)) {
        return;
      }
      break;
    case 'r':
      if (!runMovementCommand(innerAxle, true, true)) {
        return;
      }
      break;
    case 'a':
      if (!runMovementCommand(outerAxle, true, true)) {
        return;
      }
      break;
    case 's':
      if (!runMovementCommand(outerAxle, true, false)) {
        return;
      }
      break;
    case 'd':
      if (!runMovementCommand(outerAxle, false, false)) {
        return;
      }
      break;
    case 'f':
      if (!runMovementCommand(outerAxle, false, true)) {
        return;
      }
      break;
    case 'g':
      runSetCommand();
      break;
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
      if (moveGapUs >= 100UL) {
        moveGapUs -= 100UL;
      }
      break;
    case ']':
      moveGapUs += 100UL;
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

  publishAxle(innerAxle);
  publishAxle(outerAxle);
  applyCarrierPeriod();
  printHelp();
}

void loop() {
  while (Serial.available() > 0) {
    handleCommand(normalizeLowerChar((char)Serial.read()));
  }
}
