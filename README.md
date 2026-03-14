# pete-clock
Making a Humans Since clock the simple way with no parts. Design calls for one AVR, one VID28-05, and one 1uF capacitor per clock face. That's it, no other parts. All motor control and inter-face communications via direct pin connections.

## simple-test

This is the very simplest first proof of concept step just to prove that the motor can be driven fast and smooth enough with direct pin connections from an Arduino.

It can, with no need to microstepping or anything complicated. 

Note that this code uses the horrible arduino IO stuff and still it is plenty fast and totally smooth and silent. Still, it should all be rewritten with direct port writes. All the 8 motor pins can be on the same PORT so this could be very elegant.

Note that the output voltage on this configuration drops to ~4.2V so we have some headroom to increase torque by just doubling up pins. Maybe makes sense for the outer axle since it seems to need more torque. 

## watch-tick 

A slight elaboration on the simple-test that so satisfyingly simulates a ticket watch.

The outer hand ticks (it really ticks too!) each second and the inner hand ticks once per minute. 

If you want the minute tick to come exactly as the seconds hand passed 12 o'clock, then wait for the next time the seconds is at the top and push the Arduino reset button exactly then. 

## watch-rolex

A slight elaboration on the watch-tick where the second hand *smoothly* sweeps one rotation per minute (like a real rolex).
The minute hand also sweeps smoothly, but less dramatically. 

This version is naïve and uses only full phases (1/3 of a step). With my hands, it is very smooth when the hand is 
going up on the left side of the face, but jerky when it goes down the right side. I think this is because with each 
phase the hand is accelerated and decelerated and so when going down it picks up speed and then "bounces" when it stops at the 
end of the phase. I think we can cure this jerk by using microsteps to achieve constant velocity (hopefully zero acceleration and jerk).
