// Instead of trying to move the hands smoothly, this sketch intentionally
// excites the motor to make audible noise. Each note oscillates around a
// center phase with the repeating pattern:
//   center + 1, center, center - 1, center
// Each state is held for one quarter of the note period.


#include <Arduino.h>
#include <digitalWriteFast.h>

const uint8_t PHASE_COUNT = 6;
const uint8_t CONTACT_COUNT = 4;

const uint8_t phases[PHASE_COUNT][CONTACT_COUNT] = {
  {1, 0, 0, 1},
  {0, 0, 0, 1},
  {0, 1, 1, 1},
  {0, 1, 1, 0},
  {1, 1, 1, 0},
  {1, 0, 0, 0},
};

#define INNER_PIN_1 5
#define INNER_PIN_2 4
#define INNER_PIN_3 7
#define INNER_PIN_4 6

#define OUTER_PIN_1 9
#define OUTER_PIN_2 8
#define OUTER_PIN_3 3
#define OUTER_PIN_4 2

// The outer axle seems best for loudness because it has the
// larger geared output. Keep the inner axle off for now so it does not clamp
// or load the sound-producing motion. 
const bool DRIVE_INNER_AXLE = false;
const bool DRIVE_OUTER_AXLE = true;

// These are the electrical center phases the pattern returns to on every cycle.
const uint8_t INNER_CENTER_PHASE = 0;
const uint8_t OUTER_CENTER_PHASE = 0;

const uint16_t MELODY_LOOP_GAP_MS = 600;

const uint16_t NOTE_REST = 0;

struct MelodyNote {
  uint16_t frequencyHz;
  uint16_t durationMs;
};

// Notes come from here...
// https://www.youtube.com/watch?v=auxU3_2hhqk

const MelodyNote melody[] = {
  {98, 1091},
  {147, 955},
  {0, 136},
  {131, 136},
  {123, 136},
  {110, 136},
  {0, 136},
  {196, 1091},
  {147, 409},
  {0, 136},
  {131, 136},
  {123, 136},
  {110, 136},
  {0, 136},
  {196, 1091},
  {147, 409},
  {0, 136},
  {131, 136},
  {123, 136},
  {131, 136},
  {0, 136},
  {110, 1091},
};

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

void driveOffsetFromCenter( uint8_t phaseIndex) {
  writeInnerPhase( phaseIndex );
}

void delay_us(uint32_t durationUs) {
  const uint32_t end = micros() + durationUs;
  
  while (micros() < end);
}


void playNote(uint16_t frequencyHz, uint16_t durationMs) {
  if (frequencyHz == NOTE_REST) {
    delay(durationMs);
    return;
  }
  
  uint32_t singleCycleUs = 1000000UL / frequencyHz;

  uint32_t subCycleUs = singleCycleUs / 6 ;
    
  // Lets firgure out how many loops we need ahead of time to avoid all the calls to millis.
  
  unsigned long loops = (durationMs * 1000UL) /  singleCycleUs;
  
  // This pattern was inspired by the orginal bug that skipped a step and was sort of loud, but it does not work. :/

  
  while (loops--) {

    driveOffsetFromCenter(0);
    delay_us( subCycleUs );
    
    driveOffsetFromCenter(1);
    delay_us( subCycleUs );

    driveOffsetFromCenter(2);    
    delay_us( subCycleUs );
    
    driveOffsetFromCenter(3);
    delay_us( subCycleUs );
    
    driveOffsetFromCenter(4);    
    delay_us( subCycleUs );
    
    driveOffsetFromCenter(5);    
    delay_us( subCycleUs );
    
  }
  
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

  driveOffsetFromCenter(0);
}

void loop() {
  for (uint8_t i = 0; i < (sizeof(melody) / sizeof(melody[0])); ++i) {
    playNote(melody[i].frequencyHz, melody[i].durationMs);
  }

  delay(MELODY_LOOP_GAP_MS);
}
