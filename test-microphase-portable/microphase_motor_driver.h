#ifndef MICROPHASE_MOTOR_DRIVER_H
#define MICROPHASE_MOTOR_DRIVER_H

#include <Arduino.h>
#include <util/atomic.h>

#define THROW_ERROR_IF_NOT_FAST
#include <digitalWriteFast.h>

template <bool Condition, typename T = void>
struct microphase_enable_if {};

template <typename T>
struct microphase_enable_if<true, T> {
  typedef T type;
};

template <uint8_t PinCountValue, uint8_t PhaseCountValue>
struct microphase_motor_spec_t {
  static const uint8_t pin_count = PinCountValue;
  static const uint8_t phase_count = PhaseCountValue;
};

template <typename MotorSpec>
struct microphase_motor_runtime_t {
  uint8_t blendValue;
  uint8_t accumulator;
  uint8_t isPhaseB;
  uint8_t pinDiffers[MotorSpec::pin_count];
};

template <typename... MotorSpecs>
class microphase_motor_driver;

template <>
class microphase_motor_driver<> {
 public:
  inline void begin() const {}
  inline void tick() {}

  template <uint8_t MotorIndex>
  inline void publish(uint8_t, uint8_t, uint8_t) {
    static_assert(MotorIndex != MotorIndex, "motor index out of range");
  }

  template <uint8_t MotorIndex>
  inline void publish(uint8_t) {
    static_assert(MotorIndex != MotorIndex, "motor index out of range");
  }
};

template <typename FirstMotorSpec, typename... RemainingMotorSpecs>
class microphase_motor_driver<FirstMotorSpec, RemainingMotorSpecs...>
    : private microphase_motor_driver<RemainingMotorSpecs...> {
 private:
  using base_t = microphase_motor_driver<RemainingMotorSpecs...>;

  microphase_motor_runtime_t<FirstMotorSpec> runtime_;

  template <typename MotorSpec, uint8_t PinIndex>
  static inline typename microphase_enable_if<
      (PinIndex == MotorSpec::pin_count), void>::type
  beginPins() {}

  template <typename MotorSpec, uint8_t PinIndex>
  static inline typename microphase_enable_if<
      (PinIndex < MotorSpec::pin_count), void>::type
  beginPins() {
    pinModeFast(MotorSpec::pins[PinIndex], OUTPUT);
    beginPins<MotorSpec, PinIndex + 1>();
  }

  template <typename MotorSpec, uint8_t PhaseIndex, uint8_t PinIndex>
  static inline typename microphase_enable_if<
      (PinIndex == MotorSpec::pin_count), void>::type
  writePhasePins() {}

  template <typename MotorSpec, uint8_t PhaseIndex, uint8_t PinIndex>
  static inline typename microphase_enable_if<
      (PinIndex < MotorSpec::pin_count), void>::type
  writePhasePins() {
    digitalWriteFast(MotorSpec::pins[PinIndex],
                     MotorSpec::phases[PhaseIndex][PinIndex]);
    writePhasePins<MotorSpec, PhaseIndex, PinIndex + 1>();
  }

  template <typename MotorSpec, uint8_t PhaseIndex>
  static inline typename microphase_enable_if<
      (PhaseIndex == MotorSpec::phase_count), void>::type
  writePhaseByIndex(uint8_t) {}

  template <typename MotorSpec, uint8_t PhaseIndex>
  static inline typename microphase_enable_if<
      (PhaseIndex < MotorSpec::phase_count), void>::type
  writePhaseByIndex(uint8_t phaseIndex) {
    if (phaseIndex == PhaseIndex) {
      writePhasePins<MotorSpec, PhaseIndex, 0>();
      return;
    }

    writePhaseByIndex<MotorSpec, PhaseIndex + 1>(phaseIndex);
  }

  template <typename MotorSpec, uint8_t PinIndex>
  static inline typename microphase_enable_if<
      (PinIndex == MotorSpec::pin_count), void>::type
  prepareTransition(microphase_motor_runtime_t<MotorSpec> &, uint8_t, uint8_t) {}

  template <typename MotorSpec, uint8_t PinIndex>
  static inline typename microphase_enable_if<
      (PinIndex < MotorSpec::pin_count), void>::type
  prepareTransition(microphase_motor_runtime_t<MotorSpec> &runtime,
                    uint8_t phaseA,
                    uint8_t phaseB) {
    const uint8_t phaseAValue = MotorSpec::phases[phaseA][PinIndex];
    const uint8_t phaseBValue = MotorSpec::phases[phaseB][PinIndex];

    runtime.pinDiffers[PinIndex] = (phaseAValue != phaseBValue) ? 1 : 0;

    prepareTransition<MotorSpec, PinIndex + 1>(runtime, phaseA, phaseB);
  }

  template <typename MotorSpec, uint8_t PinIndex>
  static inline typename microphase_enable_if<
      (PinIndex == MotorSpec::pin_count), void>::type
  toggleChangedPins(const microphase_motor_runtime_t<MotorSpec> &) {}

  template <typename MotorSpec, uint8_t PinIndex>
  static inline typename microphase_enable_if<
      (PinIndex < MotorSpec::pin_count), void>::type
  toggleChangedPins(const microphase_motor_runtime_t<MotorSpec> &runtime) {
    if (runtime.pinDiffers[PinIndex] != 0) {
      digitalToggleFast(MotorSpec::pins[PinIndex]);
    }

    toggleChangedPins<MotorSpec, PinIndex + 1>(runtime);
  }

  template <typename MotorSpec>
  static inline void publishMotor(microphase_motor_runtime_t<MotorSpec> &runtime,
                                  uint8_t phaseA,
                                  uint8_t phaseB,
                                  uint8_t blendValue) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      writePhaseByIndex<MotorSpec, 0>(phaseA);
      prepareTransition<MotorSpec, 0>(runtime, phaseA, phaseB);
      runtime.blendValue = blendValue;
      runtime.accumulator = 0;
      runtime.isPhaseB = 0;
    }
  }

  template <typename MotorSpec>
  static inline void publishBlendValue(
      microphase_motor_runtime_t<MotorSpec> &runtime,
      uint8_t blendValue) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      runtime.blendValue = blendValue;
    }
  }

  template <typename MotorSpec>
  static inline void tickMotor(microphase_motor_runtime_t<MotorSpec> &runtime) {
    const uint8_t oldAccumulator = runtime.accumulator;
    const uint8_t nextAccumulator = (uint8_t)(oldAccumulator + runtime.blendValue);
    const uint8_t desiredPhaseB = (nextAccumulator < oldAccumulator) ? 1 : 0;

    runtime.accumulator = nextAccumulator;
    if (desiredPhaseB == runtime.isPhaseB) {
      return;
    }

    runtime.isPhaseB = desiredPhaseB;
    toggleChangedPins<MotorSpec, 0>(runtime);
  }

 public:
  microphase_motor_driver() : runtime_(), base_t() {}

  inline void begin() {
    beginPins<FirstMotorSpec, 0>();
    base_t::begin();
  }

  inline void tick() {
    tickMotor<FirstMotorSpec>(runtime_);
    base_t::tick();
  }

  template <uint8_t MotorIndex>
  inline typename microphase_enable_if<(MotorIndex == 0), void>::type
  publish(uint8_t phaseA, uint8_t phaseB, uint8_t blendValue) {
    publishMotor<FirstMotorSpec>(runtime_, phaseA, phaseB, blendValue);
  }

  template <uint8_t MotorIndex>
  inline typename microphase_enable_if<(MotorIndex != 0), void>::type
  publish(uint8_t phaseA, uint8_t phaseB, uint8_t blendValue) {
    base_t::template publish<MotorIndex - 1>(phaseA, phaseB, blendValue);
  }

  template <uint8_t MotorIndex>
  inline typename microphase_enable_if<(MotorIndex == 0), void>::type
  publish(uint8_t blendValue) {
    publishBlendValue<FirstMotorSpec>(runtime_, blendValue);
  }

  template <uint8_t MotorIndex>
  inline typename microphase_enable_if<(MotorIndex != 0), void>::type
  publish(uint8_t blendValue) {
    base_t::template publish<MotorIndex - 1>(blendValue);
  }
};

#endif
