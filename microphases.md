# Some notes on microphases

## The VID28-05

(from the datasheet)
1 full rotation of axle = 180 rotations of the rotor
1 rotation of the rotor = 6 phases

A phase is one of the 6 rotation angles that we can move the rotor to using digital signals on the 2 coils. This 
a result of the way the two coils effectively drive 3 stators inside the motor. Each phase is 60 degrees. 

So that's (6 phases/rotor rotation) * (180 rotor rotations/axle rotation) = (1080 phases/axle rotation)

(360 degrees/axle rotation) / (1080 phases/axle rotation) = (1/3 axle degree / phase)

...or (3 phases/axle degree). 

## The problem we are solving

This seems like fine control, but 1/3 a degree is visible in this system and empirically this is not fine enough 
for smooth and quiet motion at low speeds.

We want to keep  the hand to keep moving through those phase boundaries instead of stopping at each one.

## The main idea

We create "subphases". We divide the 60 degrees between two phases into 256 subphases.  Each subphase corresponds to an
angle between the two adjacent phases (but note that these are not evenly spaced due to the geometry of the motor). 

In our code, we call the two phases A and B and we call the ratio of the time we spend on B the "blend". So a
blend of 0 means we are on A all of the time, a bland of 128 means we are on A and B each 1/2 the time, 
and a blend of 255 means we are on B 255/256 of the time. If we want to be on B all of the time, then we just
switch B to A and set the blend to 0.

If we switch back and forth between A and B fast enough, then the inductance of the coils and the inertia of the rotor and 
everything attached to them will average out the high speed back and forth and we will end up with the average net magnetic 
field that drives the rotor pointing at the specific subphase angle.

### Efficiency

We picked 256 subphases/phase because we can deal with the numbers efficiently inside the function that is very rapidly updating the state of the pins that drive the coils. We call that function "tick()", and we want it to execute as quickly as possible
because the faster it updates, the more often we can call it, and the more often we call it, the more accurate 
the average output values of the pins will be and also the higher the pitch of any sounds we create (and if we can
keep those sounds about 20Khz then humans can not hear them).

Note that the blend value is only time based. We do this again to keep the processing inside the tick() function that 
rapidly updates the pins as quick as possible. To find what blend value corresponds to a given rotation angle, we need to use
a sine-based function, and that happens outside of that very fast tick() function.

## Why this looks like PWM, but is not plain PWM

The blend is basically the duty cycle of B as 8-bit number (we get A the rest of the time when B is not selected).

That is true at a high level, but we are not using a fixed PWM frame. Instead, we use a first-order delta-sigma style accumulator. On each update at the carrier frequency, we:

1. add `blend` into an accumulator
2. if it overflows, select phase B
3. otherwise select phase A

(Note that when an 8-bit value overflows, we lose the carry bit so we effectively end up with `accumulator=(accumulator+blend)%256)`, just more efficient to compute)

This spreads the B periods out over time instead of bunching them into a fixed repeating block.

It is like dithering in an image to blend colors.

That matters because:

- it avoids introducing a strong low-frequency PWM frame artifact (quieter to spread out the energy across freqs)
- it gives a smoother average torque
- it is tiny and cheap to implement on an 8-bit AVR

### Example

Suppose `blend = 37`, which means we want `B` about `37/256` of the time.
Here is what the first 20 carrier updates look like:

For this example:

- `blend = 37`
- overflow threshold = `256`

This is the actual scale used by the code.

| Update | Accumulator Before | Add 37 | Overflow? | Output | Accumulator After |
| --- | ---: | ---: | --- | --- | ---: |
| 1 | 0 | 37 | No | A | 37 |
| 2 | 37 | 74 | No | A | 74 |
| 3 | 74 | 111 | No | A | 111 |
| 4 | 111 | 148 | No | A | 148 |
| 5 | 148 | 185 | No | A | 185 |
| 6 | 185 | 222 | No | A | 222 |
| 7 | 222 | 259 | Yes | B | 3 |
| 8 | 3 | 40 | No | A | 40 |
| 9 | 40 | 77 | No | A | 77 |
| 10 | 77 | 114 | No | A | 114 |
| 11 | 114 | 151 | No | A | 151 |
| 12 | 151 | 188 | No | A | 188 |
| 13 | 188 | 225 | No | A | 225 |
| 14 | 225 | 262 | Yes | B | 6 |
| 15 | 6 | 43 | No | A | 43 |
| 16 | 43 | 80 | No | A | 80 |
| 17 | 80 | 117 | No | A | 117 |
| 18 | 117 | 154 | No | A | 154 |
| 19 | 154 | 191 | No | A | 191 |
| 20 | 191 | 228 | No | A | 228 |

So the first 20 outputs are:

```text
AAAAAABAAAAAABAAAAAA
```

That gives us 2 `B` outputs out of the first 20 updates. That is not exactly `37/256` yet, because 20 carrier ticks is a very short window. Over a longer run, the average converges toward `37/256`.

The important part is not just the ratio. It is that the `N` outputs get spread out over time:

- delta-sigma style: `AAAAAABAAAAAABAAAAAA...`
- fixed-frame PWM style: all the `B` pulses would bunch together into one block inside a repeating frame

<b>Both can reach the same long-term average, but the delta-sigma version spreads the energy out and avoids the hard low-frequency frame boundary.</b>

## Our motor-driver abstraction

We have a motor driver implemented in a C++ header file that implements this abstraction. It lets you specify a number of motors, and for each motor the number of phases and the pin states for each of those phase.

After that, it lets you update which two phases are currently A and B, and the blend value between them. (Or update only the
bland if A and B do not change.)

I thought about adding in the motor control layer too, which would have abstracted away converting axle rotation
angles into A,B, and blend using the sine wave calculations but I could not find the exact right way to express this
that would be flexible for all the cases we already have just in this repo. Hopefully eventually I will figure it out,
and also maybe be able to add in support for commanded position sequences with acceleration and deceleration curves.