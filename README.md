# pete-clock
Making a Humans Since clock the simple way with no parts. Design calls for one AVR, one VID28-05, and one 1uF capacitor per clock face. That's it, no other parts. All motor control and inter-face communications via direct pin connections.

Note that for some of these demos you will need the `digitalWriteFast` library. To install that in the Arduino IDE, got to Tools->Manage Libraries and find and install it. 

## simple-test

This is the very simplest first proof of concept step just to prove that the motor can be driven fast and smooth enough with direct pin connections from an Arduino.

It can, with no need to microstepping or anything complicated. 

Note that this code uses the horrible arduino IO stuff and still it is plenty fast and totally smooth and silent. Still, it should all be rewritten with direct port writes. All the 8 motor pins can be on the same PORT so this could be very elegant.

Note that the output voltage on this configuration drops to ~4.2V so we have some headroom to increase torque by just doubling up pins. Maybe makes sense for the outer axle since it seems to need more torque. 

## watch-tick 

A slight elaboration on the simple-test that so satisfyingly simulates a ticket watch.

The outer hand ticks (it really ticks too!) each second and the inner hand ticks once per minute. 

If you want the minute tick to come exactly as the seconds hand passed 12 o'clock, then wait for the next time the seconds is at the top and push the Arduino reset button exactly then. 

## test-microphase

To do anything smoother and quieter at slow speeds, we are going to need to use microphases - which is
a way to move the rotor to a position between two phases (this motor has 6 phases per step and 360 steps per rotation). 

So this sketch presents our solution to the problem - a general purpose driver that can move the 
rotor to any one of 256 subphase positions inside each phase. 

The microphase implementation is more complicated than straight PWM, so there is [a full description of 
how microphases work](watch-rolex-microphases.md) if you care, but the take away is that it lets us step at very low speeds like 1RPM very smoothly and quietly by `blending` between two adjacent phases as we go around. (A phase is 1/3 of a step and is the smallest amount of movement we can create on this motor with static digital signals)

## watch-rolex

A Rolex-style watch face driven through the new subphase driver.
The second hand advances exactly 8 times per second, and the minute hand
advances on every same beat by 1/60th as much on average - just like a real
Rolex. It uses our new microphase framework to be able to hit all of those
positions, many of which do not land exactly on even phase locations.

## step-wars

For Ian. Instead of trying to be quiet, we embrace the loud. This is as loud as I could make it, but sadly, it is still not loud.