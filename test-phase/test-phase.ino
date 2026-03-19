
// see "driving puls ein digital mode" in the datasheet
// Sequncing from the VID28-05 dtatasheet using thier names

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


void setup() {
  // Set all motor pins as outputs
  // Some day We might want to float pins sometimes to avoice cogging?
  for (int i = 0; i < CONTACT_COUNT; i++) {
    pinMode(motorPins[INNER_AXLE][i], OUTPUT);
    pinMode(motorPins[OUTER_AXLE][i], OUTPUT);
  }
  
  // We dont need any interrupt so reduce jitter (although it probably doesnt matter for this test were all times are OTO 1ms)
  noInterrupts();
}

byte inner_phase = 0; 
byte outer_phase = 0;

void loop() {
  
  // 1 degree per step, 3 partial-steps per step
  
  for( unsigned l=0; l< 180 *3 ; l++ ) {
  
    // Apply the current phase to the pins
    // This should really be done with a single write to a port register
    for (int i = 0; i < CONTACT_COUNT; i++) {    
      digitalWrite(motorPins[INNER_AXLE][i], phases[inner_phase][i]);
      digitalWrite(motorPins[OUTER_AXLE][i], phases[outer_phase][i]);
    }

    // Note that the two axles seem to be geared in opposite directions. 
    
    // outer axle backward (Clockwise)
    inner_phase++;
    if (inner_phase >= PHASE_COUNT ) {
      inner_phase = 0;
    } 

    

    // Outter axle forward (Counter - Clockwise)
    outer_phase++;
    if (outer_phase >= PHASE_COUNT) {
      outer_phase = 0; // Loop back to start
    }
    
    // longer delay = slower spin
    // This is the emperically determined fastest velocity we can support starting from a dead stop before we start skipping steps. 
    // This is depensand on lots of things, including the monent of inertia of the hands.
    // Note that we can do much better than this if we implkement acceleratiuon and/or microsteps and/or increased current via doubling up pins and/or increasing Vcc. 
    _delay_us(800);
  }
    
  // Pause after each half rotation 
  _delay_us(1000000);   // 1s
}
