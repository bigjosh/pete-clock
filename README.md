# pete-clock
Making a Humans Since clock the simple way with no parts. Design calls for one AVR, one VID28-05, and one 1uF capacitor per clock face. That's it, no other parts. All motor control and inter-face communications via direct pin connections.

Note that for some of these demos you will need the `digitalWriteFast` and the `TimerOne` libraries. To install in the Arduino IDE, got to Tools->Manage Libraries and find and install them. 

## test-phase

The simplest possible sketch to prove that we can directly drive these motors with an Arduino. Used only pure digital pin writes to 
cycle thru the phases with a fixed delay between each step. 

## simple-test

This is a first proof of concept to see if the motor can be driven fast and smooth enough with direct pin connections from an Arduino.

It can- and with no need to microstepping or anything complicated. 

Note that this code uses the horrible arduino IO stuff and still it is plenty fast and totally smooth and silent. Still, it should all be rewritten with direct port writes. All the 8 motor pins can be on the same PORT so this could be very elegant.

Note that the output voltage across the coils on this configuration drops to ~4.2V so we have some headroom to increase torque by increasing current by just doubling up pins.

## watch-tick 

A slight elaboration on the simple-test that so satisfyingly simulates a ticking watch.

The outer hand ticks by every second quickly advancing thru 18 phases (1/60 of a full rotation) to get to the position - and then stops dead. The inner hand ticks once per minute.

If you want the minute tick to come exactly as the seconds hand passes 12 o'clock, then wait for the next time the seconds is at the top and push the Arduino reset button exactly then. To be authentic, I think the minute should advance with each second tick
to simulate watch gears? That would be (6*180)/3600=1/3 phase per second, so we could keep a counter and advance the minute hand one
phase every 3 second ticks.

## test-microphase-portable

To do anything smoother and quieter at slow speeds, we are going to need to use microphases - which is
a way to move the rotor to a position between two phases (this motor has 6 phases per step and 360 steps per rotation).

So this sketch presents our solution to the problem - a general purpose driver that can move the 
rotor to any one of 256 subphase positions inside each phase. 

The microphase implementation is more complicated than straight PWM, so there is [a full description of 
how microphases work](microphases.md) if you care, but the take away is that it lets us step at very low speeds like 1RPM very smoothly and quietly by `blending` between two adjacent phases as we go around.

This version is "portable" because it only uses the libs TimerOne and DigitWriteFast to access the hardware. There 
is another (non-published) version that uses direct registers and hand written ASM to make the time spent in the 
tick function *much* shorter - which allows much higher carrier frequencies. But lets see how far we can get with the
portable version first.

## watch-rolex

A Rolex-style watch face driven through the new subphase driver.
The second hand advances exactly 8 times per second ("beats"), and the minute hand
advances on every same beat by 1/60th as much on average - just like a real
Rolex.

Since 1/60th of a rotation = 18 phases, and 18 is not evenly divisible by 8, we can not be be exactly accurate using only phases.
So instead we use our new microphase framework to be able to exactly hit all of those precise positions.

TBH, I do not think you can see the angular difference of 1 phase so it would probably *look* better if we instead stuck to round phases and cycled the phases-per-beat like 2,2,2,3,2,2,2,3 = 8 beats/second = 6*3=18 phases per second. 

## watch-aramtron

The Armatron is the smoothest watch I have ever seen, and this sketch aims to emulate that by being
as smooth as we can possibly be using our subphases driver. To eliminate all jitter and acceleration,
we update the subphase position with every time we update the pin states, running off of the fixed
period timer. We set the timer so that one every timer fire, we move exactly one microphase. 

Notes:
1. The timer on this chip, the closest available frequency to 1 microphase/cycle is off by a tiny bit so
this watch runs at 1.000064 RPM (3.84 ms fast per revolution). Sorry. Do also note that the internal 
oscillator on this chip is only accurate to +/-5% so don't be too mad if you are late for dinner. 

2. An actual Aramtron runs off of a 360Hz tuning fork. We could emulate this and match even the hum of a real aramtron.

3. I am noticing some slop in the gears, particularly when the hand is going down so gravity is with us. If needed,
maybe we can do some fancy anti-backlash and always turn the rotor a little tiny bit in the opposite direction
after every stop to take up some of the slack?

## step-wars

For Ian. Instead of trying to be quiet, we embrace the loud. This is as loud as I could make it, but sadly, it is still not loud.
