// A demo of a watch face with a second hand that *smoothly* spins one rotation per minute (like a real rolex).

// 360 steps per rev * 3 phases per step = (360*3) phases per rev
// (360*3) phases per rev / 60 seconds per rev = 18 phases per second
// 1s * (1000ms per second )/ (18 phases phases per second) = ~55.5555555556ms per phase
// moveovere, we can not generate an exact clock that evenly goes into 18Hz from the 16Mhz clock on the Arduino so 
// we have two choices - add leap phases with a breshinghams style "bouince around the exact value", or...
// be lazy and just round up to 56ms per tick. I am lazy, and the arduino clock can be off by 5% anyway so who cares.

#define MILLIS_PER_SEC_PHASE 56
#define SEC_PER_MIN 60
#define MILLIS_PER_MIN_PHASE (MILLIS_PER_SEC_PHASE*SEC_PER_MIN)

// It takes 6 phase per step
#define PHASE_COUNT 6

// There are 4 pins per axle. The datsheet calls them CONATC1 - CONATCT4. 
#define CONTACT_COUNT 4


// We execute these steps on the output pins in order to turn
// again taken directly from the datasheet, even using thier names
// Note that Contacts 2 and 3 are always the same. The datasheets combines them into a single trace.
const int phases[PHASE_COUNT][CONTACT_COUNT] = {
  {1,0,0,1},    //1
  {0,0,0,1},    //2
  {0,1,1,1},    //3
  {0,1,1,0},    //4
  {1,1,1,0},    //5
  {1,0,0,0},    //6
};


// Pins from VID28 datasheet . Also called contacts.
// First is the pin name in the datasheet, next is the color of our wire, last is the Arduino IO pin it is connected to
// Inner motor - 
//  A1 = BLUE = 5
//  A2 = PURPLE = 4
//  A3 = YELLOW = 7
//  A4 = GREEN = 6

// B1 - RED - 9
// B2 - ORANGE - 8
// B3 - GREY - 3
// B4 - WHITE 2

#define INNER_AXLE   0
#define OUTER_AXLE  1

const int motorPins[2][CONTACT_COUNT] = {
  {5, 4, 7, 6},   // Inner axle  
  {9, 8, 3, 2},   // Outer axle
};

unsigned long next_sec_phase;
unsigned long next_min_phase;

void setup() {
  // Set all motor pins as outputs
  // Some day We might want to float pins sometimes to avoice cogging?
  for (int i = 0; i < CONTACT_COUNT; i++) {
    pinMode(motorPins[INNER_AXLE][i], OUTPUT);
    pinMode(motorPins[OUTER_AXLE][i], OUTPUT);
  }
  
  unsigned long now = millis(); 
  next_sec_phase  = now + MILLIS_PER_SEC_PHASE;
  next_min_phase = now + MILLIS_PER_MIN_PHASE;
  
}

byte inner_phase = 0; 
byte outer_phase = 0;

void loop() {
  
  // We need to snapshot this so we don't lose ticks 
  unsigned long now = millis();
  
  if ( now >= next_sec_phase ) {
    
    // Outter axle seconds ( Clockwise is backwards becuase of gearing )
    if (outer_phase == 0 ) {
      outer_phase = PHASE_COUNT;
    }    
    outer_phase--;
    

    for (int i = 0; i < CONTACT_COUNT; i++) {    
      digitalWrite(motorPins[OUTER_AXLE][i], phases[outer_phase][i]);
    }
    
    next_sec_phase = now + MILLIS_PER_SEC_PHASE;
    
  }

  if ( now >= next_sec_phase ) {
  
      // minute hand on inner axle (Clockwise)
      inner_phase++;
      if (inner_phase >= PHASE_COUNT ) {
        inner_phase = 0;
      }

    for (int i = 0; i < CONTACT_COUNT; i++) {    
      digitalWrite(motorPins[INNER_AXLE][i], phases[inner_phase][i]);
    }
    
    next_min_phase = now + MILLIS_PER_MIN_PHASE;
    
  }   
}