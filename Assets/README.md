# Assets — the instructor clip pack goes here

The **instructor clip pack** (download from **myCourses**) goes in this directory.
**Do not commit audio to the repo.**

Expected files:

| File | Content | Expected band (spec §7.3) |
|---|---|---|
| `s1s2.wav` | clean S1/S2 heart sounds | energy < 150 Hz |
| `murmur.wav` | pathological murmur | energy > 200 Hz (trips the HEART marker) |
| `wheeze.wav` | wheeze | dominant ≥ 400 Hz |
| `crackle.wav` | crackles | broadband |

A phone or a laptop plays the clips at the onboard mic (spec §7.1).
The clips are PHI-free stimuli with known ground truth.
The demo uses the clips of this pack, so all benches see the same stimuli.
