# Some notes on how `watch-rolex` works

This note explains the idea behind `watch-rolex.ino` for a reader who already understands PWM, but may not have seen this exact delta-sigma style trick before. it is more complicated, but it is
worth it for this application where Pete cares so dearly about quietness at all speeds. 

The short version is:

- the VID28 only gives us a small number of valid _digital_ drive states (6)
- we want to be able to move to/thru locations between these states
- we could *buy* stepper motor drivers- but these add cost and complexity
- instead, we switch quickly between two neighboring real states
- we choose how often to use each state with a small delta-sigma accumulator
- if we do it fast enough, the inductance of the coils and the mass of the moving parts smooth it out

## The problem we are solving

At higher speeds, we have the inertia of the system is working with us to move 
thru the positions. But at 1RPM, there is time for us to slow down and speed up on each phase.

The naive rolex sketch held each motor phase for a noticeable chunk of time, then jumped to the next one. Even though the hand was stepping pretty often, it was still a stop-and-go motion:

- energize one phase
- let the hand settle there
- jump to the next phase
- settle again

This makes noise and makes the motion jerky when we induce vibrations in the hand/rotor system.

We want to keep  the hand to keep moving through those phase boundaries instead of stopping at each one.

## The main idea

We keep the six real VID28 drive states exactly as they are.

At any instant, each axle is considered to be somewhere between two adjacent states:

- `basePhase`
- `nextPhase`
- `blendWeight`

If `blendWeight` is `0`, we always output `basePhase`.

If `blendWeight` is near full scale (255), we output `nextPhase` most of the time.

If it is somewhere in the middle, we alternate between the two in a controlled way so that the average drive vector reflects the ratio between the two.

That is the whole trick.

## Why this looks like PWM, but is not plain PWM

"`blendWeight` is basically the duty cycle of `nextPhase` relative to `basePhase` as 8-bit number." '0' means we are 
100% at `basePhase`. `128` means we are at `basePhase` half the time and `nextPhase` half time time. 

That is true at a high level, but we are not using a fixed PWM frame like:

- 8 ticks long
- 3 ticks of `nextPhase`
- 5 ticks of `basePhase`
- repeat exactly

Instead, we use a first-order delta-sigma style accumulator. On each update at the carrier frequency, we:

1. add `blendWeight` into an accumulator
2. if it overflows, emit `nextPhase` and subtract the threshold
3. otherwise emit `basePhase`

This spreads the `nextPhase` samples out over time instead of bunching them into a fixed repeating block.

It is like dithering in an image to blend colors.

That matters because:

- it avoids introducing a strong low-frequency PWM frame artifact (quieter to spread out the energy across freqs)
- it gives a smoother average torque
- it is tiny and cheap to implement on an 8-bit AVR

Just think of this as "duty cycle without a fixed frame."

### Example

Lets use a real value from the current sine table and a real accumulator scale.
Suppose `blendWeight = 37`, which means we want `nextPhase` about `37/256` of the time.
Here is what the first 20 carrier updates look like:

For this example:

- `blendWeight = 37`
- overflow threshold = `256`
- `B` means output `basePhase`
- `N` means output `nextPhase`

This is the actual scale used by the code.

| Update | Accumulator Before | Add 37 | Overflow? | Output | Accumulator After |
| --- | ---: | ---: | --- | --- | ---: |
| 1 | 0 | 37 | No | B | 37 |
| 2 | 37 | 74 | No | B | 74 |
| 3 | 74 | 111 | No | B | 111 |
| 4 | 111 | 148 | No | B | 148 |
| 5 | 148 | 185 | No | B | 185 |
| 6 | 185 | 222 | No | B | 222 |
| 7 | 222 | 259 | Yes | N | 3 |
| 8 | 3 | 40 | No | B | 40 |
| 9 | 40 | 77 | No | B | 77 |
| 10 | 77 | 114 | No | B | 114 |
| 11 | 114 | 151 | No | B | 151 |
| 12 | 151 | 188 | No | B | 188 |
| 13 | 188 | 225 | No | B | 225 |
| 14 | 225 | 262 | Yes | N | 6 |
| 15 | 6 | 43 | No | B | 43 |
| 16 | 43 | 80 | No | B | 80 |
| 17 | 80 | 117 | No | B | 117 |
| 18 | 117 | 154 | No | B | 154 |
| 19 | 154 | 191 | No | B | 191 |
| 20 | 191 | 228 | No | B | 228 |

So the first 20 outputs are:

```text
BBBBBBNBBBBBBNBBBBBB
```

That gives us 2 `N` outputs out of the first 20 updates. That is not exactly `37/256` yet, because 20 carrier ticks is a very short window. Over a longer run, the average converges toward `37/256`.

The important part is not just the ratio. It is that the `N` outputs get spread out over time:

- delta-sigma style: `BBBBBBNBBBBBBN...`
- fixed-frame PWM style: all the `N` pulses would bunch together into one block inside a repeating frame

<b>Both can reach the same long-term average, but the delta-sigma version spreads the energy out and avoids the hard low-frequency frame boundary.</b>

## The two time scales in the code

The code runs on two very different time scales.

### 1. Slow control path in `loop()`

`loop()` decides where each hand should be right now based on absolute elapsed time.

It does not say "take one more step." It says "given the current time, the hand should be at this subphase position."

That is why the sketch recomputes position from scratch with `positionInRotation(...)` instead of incrementing a phase counter forever.

This keeps the motion model simple and keeps both hands locked to time.

We just have to make sure we do not ask too much and try to move faster than physically possible. 

### 2. Fast carrier async callback in `onCarrierTick()`

The TimerOne callback runs every `32 us`, about `31.25 kHz`.

That callback does the fast averaging:

- choose either `basePhase` or `nextPhase` based on the accumulator value
- write the chosen real phase to the motor pins

The slow path decides the target. The fast path makes it physically smooth.

## What an "axle" stores

Each hand has an `AxleState`:

- `rotationPeriodUs`
- `direction`
- `lastSubphasePosition`
- `basePhase`
- `nextPhase`
- `blendWeight`
- `blendAccumulator`

The first three are really about the motion model.

The last four are the live state that the ISR needs in order to dither between phases.

Someday we can break these apart so that the subphase stuff can work across any movement routine - but for now we go with simpler code.

## How a target position becomes a pair of phases

Each full rotation is divided into:

- `1080` coarse phase positions (`360 * 3`)
- `8` subphase positions in between one coarse position and the next coarse position

So one rotation is `8640` subphase positions total.

For a given axle:

1. `positionInRotation(...)` returns a subphase position from `0` to `8639`
2. `phaseCount = subphasePosition / SUBPHASES_PER_PHASE`
3. `subphase = subphasePosition % SUBPHASES_PER_PHASE`
4. `basePhase` is derived from `phaseCount` and axle direction
5. `nextPhase` is just the adjacent phase in that same direction
6. `blendWeight` comes from the 8-entry sine-shaped lookup table

That lookup table is:

```
{ 0, 10, 37, 79, 128, 176, 218, 245 }
```

It is not trying to be a perfect analog sine current profile. It is just a smooth monotonic easing curve from one discrete phase to the next, and it seems to be good enough. After the `245` entry, the code advances to the next phase pair and resets `blendWeight` to `0`. At that point, the old `nextPhase` has become the new `basePhase`, so the effective output continues smoothly at what is essentially full-on for that phase.

## What `blendedPhase()` is doing

This is the delta-sigma part.

Each interrupt:

- start by assuming we will output `basePhase`
- add `blendWeight` into `blendAccumulator`
- if the accumulator crosses `256`, emit `nextPhase` instead and keep the remainder

That means:

- low `blendWeight` chooses `nextPhase` only occasionally
- medium `blendWeight` chooses `nextPhase` about half the time
- high `blendWeight` chooses `nextPhase` most of the time

The exact bit pattern is not the point. The average over many carrier ticks is the point.

## Current implementation numbers

For the current sketch:

- carrier period: `32 us`
- carrier frequency: about `31.25 kHz`
- outer hand period: `60 s` per rotation
- inner hand period: `3600 s` per rotation
- subphases per rotation: `8640`
- outer target subphase update rate: `8640 / 60 = 144 Hz`
- inner target subphase update rate: `8640 / 3600 = 2.4 Hz`

So the carrier is much faster than the motion model:

- about `217` carrier ticks per outer subphase
- about `13020` carrier ticks per inner subphase

That is why this averaging trick works so well here.

## Future work

- switch from `digitalWriteFast` to direct `PORT` writes
- replace `basePhase` and `nextPhase` with actual PORT values to make the callback superfast register lean
- Get rid of TimerOne and write our own ISR and timer setup.
- Rewrite the async callback in pure ASM (have it use preloaded values to write to PORT regs)
- raise `SUBPHASES_PER_PHASE`
- tune the blend table
