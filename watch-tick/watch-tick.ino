
// VID28-05 demo that simulates a ticking watch
// place the outer hand at center before powering on (or press reset when seconds hand is at 12 o'clock) for minutes to tick at the right time. 


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
  
}

byte inner_phase = 0; 
byte outer_phase = 0;

unsigned long next_tick = 0;

byte elapsed_secs = 0; // Use for Driving the minute (inner) hand

void loop() {
  
  // We need to snapshot this so we don't lose ticks 
  unsigned long now = millis();
  
  if ( now >= next_tick ) {
    
    
    // Keep track of elased Seconds so we can tick the minute hand
    elapsed_secs++;


    // if it is time for the next tick, we execute all the steps for that tick as quickly as possible to get that 
    // satifiying "tick" sound and motion. 

          
    // 1 degree per step, 3 partial-steps per step 
    // We want to look like a watch so one tick should be 1/60 of revoltuion, so 360 degress * 1/60th revolution per tick = 6 steps per tick
    // 3 partial steps per step * 6 partial steps per tick = 18 partial steps per tick. 
        
    for( unsigned l=0; l< 18 ; l++ ) {
      
      if (elapsed_secs == 60 ) {
        // a minute has passed, tick the minute hand
        
        // minute hand on inner axle (Clockwise)
        inner_phase++;
        if (inner_phase >= PHASE_COUNT ) {
          inner_phase = 0;
        } 
        
      }
      
      // Outter axle seconds ( Clockwise is backwards becuase of gearing )
      if (outer_phase == 0 ) {
        outer_phase = PHASE_COUNT;
      }    
      outer_phase--;
            
      // Apply the current phase to the pins
      // Yea we redundantly reapply the existing values to the minute hand on every second, but who cares cycle are free. 
      // This should really be done with a single write to a port register
      for (int i = 0; i < CONTACT_COUNT; i++) {    
        digitalWrite(motorPins[INNER_AXLE][i], phases[inner_phase][i]);
        digitalWrite(motorPins[OUTER_AXLE][i], phases[outer_phase][i]);
      }

      // Note that the two axles seem to be geared in opposite directions. 
      // longer delay = slower spin
      // This is the emperically determined fastest velocity we can support starting from a dead stop before we start skipping steps. 
      // This is depensand on lots of things, including the monent of inertia of the hands.
      // Note that we can do much better than this if we implkement acceleratiuon and/or microsteps and/or increased current via doubling up pins and/or increasing Vcc. 
      _delay_us(1000);
    }

    // tick once per second
    // the ticking actually took some time (hopefully less than 1s) but does not matter since we are using the snapshot of now
    next_tick = now+1000;     
    
    if (elapsed_secs == 60 ) {
      // clear the minute (we already ticked it)
      elapsed_secs = 0;
    }    
    
  }
}