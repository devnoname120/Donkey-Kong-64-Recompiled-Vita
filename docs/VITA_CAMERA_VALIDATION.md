# Native camera capture and save/restart validation

## Scope

The separate `DK64AdventureProbe.vpk` (`DK64RT001`) now exercises an intentional
miss followed by a successful Jungle Japes pool-fairy photograph. The regular
`DK64Recompiled.vpk` has neither this controller script nor its test fixtures.
The probe uses `ux0:data/dk64recompiled-probe/`, not the regular game's save path.

The fixture grants camera ownership (`0x179`) and ten film through the original
game APIs before entering Japes through exit 13. After the miss photograph has
been freed, it places the player at `(700, 320, 3080)` and allows the original
physics and camera to settle. This deliberately avoids making terrain navigation
a prerequisite for the camera test. It is not proof of normal quest progression,
reaching the pool from the entrance, or acquiring the camera in gameplay.

The game retains ownership of projection, depth/visibility decoding, recognition,
shutter processing, fairy collection, the permanent flag (`0x24d`), saving, the
photograph preview, reward animation and deferred buffer frees. The fixture does
not write recognition results, fairy coordinates, visibility ranges or the
collection flag. See [depth readback](VITA_DEPTH_VALIDATION.md) and
[floating-point conversion](VITA_FENV_VALIDATION.md) for the production fixes.

## Aiming and completion checks

Recognition describes an earlier projection. Firing immediately when it becomes
true can miss a moving fairy while the player's previous stick input is still
being consumed. The probe therefore releases the stick and requires recognition
and a safe projected position to remain valid before firing.

Full-strength corrections also overshot this window repeatedly. Near the target,
bit `0x20` of the probe-only controller packet selects magnitude `0.125` instead of
`0.6`. Both axes and the buttons remain in one atomic packet. Far-offscreen and
invalid-projection searches use the normal coarse input. Ordinary game controls
and the Klamour probe's existing packets are unchanged.

Completion requires both photograph buffers to have passed the original deferred
free paths, the fairy flag to be set, and player control state 12 or 13. Reward
state `0x83` is not completion. The final probe also supplies movement after the
reward, rather than treating a photograph allocation or a collection flag alone
as a completed lifecycle.

## Observed capture

On 2026-09-07, `build/vita3k-camera-validation/final-fine-capture/` passed:

- Camera entry through Z followed by a C-Down edge; the normal B shutter.
- A miss with an initially uncaptured fairy, followed by successful recognition
  and a second photograph. Both buffers were queued and freed by the game.
- All **20,480 pixels in each photograph**, compared against that photograph's
  actual game-requested source framebuffer, including tile order and sepia/alpha
  conversion. Neither source image was primed by an extra diagnostic read.
- The fairy reward, return to ordinary player control, and subsequent movement.
  The player moved from `(700, 240, 3080)` to approximately `(684, 240, 3121)`.

The original game reported the successful shutter at projection `(166, 119)`.
It reached normal control at frame 679 with two photos, fairy flag 1, and both
release masks equal to 3. The reward had increased film from 8 to 11. The process
continued until the test's 225-second timeout; exit 124 is an intentional test
termination, not evidence of graceful game or emulator shutdown.

The archived VPK SHA-256 is:

```text
0274a159920850c925ce2a428d68c4d0c7ed2b4ed6644cfbebfdefd6008bd8ed
```

## Fresh-process save verification

`build/vita3k-camera-validation/final-save-restart/` started a new emulator
process with the same VPK and the exact `DK64.bin` produced by the capture run:

```text
2ddd21c5566ade31962323055d320f120a96b200e63fffd7cae5297928f21879
```

An opt-in `verify-camera-save` file in the probe data directory selects read-only
verification. Before any fixture grant, the probe calls the original flag API and
requires both camera ownership and the captured pool-fairy flag. Missing progress
raises an error; verification never creates it. The native restart reported:

```text
Camera probe save verification passed: camera=1 fairy_flag=1 film=5; no prerequisites changed
```

This proves persistence of the two permanent flags, not persistence of every
transient ammunition count. No second photograph, fixture placement or camera/
film grant occurred. The harness stopped this process ten seconds after observing
verification; its exit 143 records that controlled termination.

## Final implementation repeat

After the tracing-wrapper fix and range-limited depth packing, the final probe
repeated the complete lifecycle in
`build/vita3k-camera-validation/final-lazy-capture/`. Both photographs again
matched all 20,480 expected pixels, both deferred frees completed, and ordinary
control returned after the reward. A fresh process in
`final-lazy-save-restart/` loaded exactly that run's save and observed camera
ownership and the captured-fairy flag without granting prerequisites.

The matching probe VPK SHA-256 is
`8656526fc015807c92ba9fa2548c0d32d8adfb108363fa97c65b2edd27214778`;
the save crossing the process boundary has SHA-256
`1b7d1a38f619b80256b8cffaf3ee13c0fcc794e2efbdcdda56e838e7d3d5e43a`.
The capture and verification processes were deliberately terminated, with exit
codes 124 and 143 respectively. CPU optimization remained disabled. These final
results supersede the earlier implementation for package validation without
changing the test fixture or its limitations.

## Rechecking the evidence

The source-derived integer oracle in `tools/compare_vita_photograph.py` checks one
photograph independently. `tools/verify_vita_camera.py` additionally checks the
ordered source readbacks, miss/success results, post-reward completion, package
hashes and exact save continuity across the process boundary:

```sh
python3 tools/verify_vita_camera.py \
    build/vita3k-camera-validation/final-lazy-capture \
    --restart build/vita3k-camera-validation/final-lazy-save-restart
python3 -m unittest discover -s tools -p 'test_verify_vita_camera.py' -v
```

The eight receipt-verifier tests include rejection of wrong pixels, premature or
missing completion, reused readbacks, changed packages, mismatched restart saves
and prerequisite grants during restart verification. The verifier rejects both
an earlier timed-out aiming run and an older run whose completion marker was
emitted during the reward animation, even though that older run did capture the
fairy and produce correct photograph pixels.

The native inputs, logs, screenshots, ROM and saves remain local; private game
assets are not committed. To build the probe, use the ordinary diagnostic Vita
build with `DK64_VITA_SCRIPTED_INPUT=ON`, `DK64_VITA_PROBE_MAP=7`,
`DK64_VITA_PROBE_CAMERA=ON`, `DK64_VITA_SCRIPTED_PAUSE=OFF`,
`DK64_VITA_PROFILE_FUNCTIONS=OFF`, and normal synchronized color readbacks.
Remove `verify-camera-save` for a new capture run; create it only for a restart
using a successfully captured save. It affects the probe title only.

## Environment and remaining limits

These game runs used the exact Linux AArch64 Vita3K `496939b6` build, Vulkan with
Mesa/Lavapipe, external-host memory mapping, and **CPU optimization disabled**.
Default CPU optimization had intermittently aborted full-game runs. The small
native depth controls pass with it enabled; that does not establish full-game
stability in the same setting, and changing the depth transfer path did not
eliminate the full-game failure.

This is native ARM game-code execution under an emulator, not physical-Vita
certification or a complete DK64 playthrough. The capture used synchronized color
readbacks. The optional hardware color-readback speedhack permits older color
images and needs separate visual/performance testing; depth queries remain
synchronous in either build.
