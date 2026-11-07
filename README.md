# AI Drive

**Beta, no promises.** For F1 25 on Steam (build 25251167 or newer) with **Swofty Launcher 1.7.0 or newer**. Tested in Grand Prix races only.

> **Please read:** this is the AI-only version of the old **AI Drive + Spectate**, and it updates it. Watching the race moved to a separate addon, **Spectator/Broadcast Mode**. Install that one too if you want to watch. Don't use the old AI Drive + Spectate together with Spectator/Broadcast Mode.

> **Known limitations (noted by the author in testing, please read first)**
> - **Races only.** Practice and qualifying don't really work: you have to send the AI out of the garage and bring it back to the garage yourself.
> - **The strategy can look odd.** The AI uses the pit plan the game built for your car and doesn't always react like the other AI cars (for example under a safety car).
> - **The formation lap tends to break it.** Turn the AI on after the start.
> - **It can lose pace over a race.** At AI difficulty 110 the car started faster than the field median but lost 0.5 to 1.5 seconds per lap against the game's own AI cars within 7 laps, mostly because its front tyres wore about 25 to 150 % faster than the AI teammate's in the same car. The cause is not known yet, so don't expect it to drive exactly like the other AI cars.
> - **New in 1.1.0 and not tested in the game yet:** the "use the AI teammate's setup" option (it uses Swofty's experimental live car operations) and the pit stop button skip.

## What is it?
Press one key and the game's own AI drives **your** car. Press it again and you drive again. Handy if you want to watch your own race, or test things without driving.

## How to use it
1. Turn the addon on in the launcher and start a race.
2. Press **F5** (or the key you picked) once the race has started. The AI takes your car.
3. Press **F5** again to take over. You can have a short countdown with beeps so you are ready.
Or switch on **"AI drives from the start"** and it takes your car by itself a few seconds after the race starts.

## Settings
- **Start: AI drives from the start of the race:** the AI takes your car by itself after the race starts. Races only.
- **Keys: AI on/off key:** the key for taking and giving back the car (default F5).
- **Keys: take-over countdown:** how long you wait before you get the car back: right away, or 3 or 5 seconds with beeps.
- **Pace: slow the AI a little:** 100 % is the AI's normal pace. Lower values make your car a bit slower. Restart the game after changing it.
- **Sound: beeps.**
- **Pit: no quick-time event while the AI drives:** with the game's "Immersive pit stops" the stop asks for a button press. The AI can't press it, so the stop came out late. On (default): the game skips that press for your car while the AI drives, and the crew does a normal stop.
- **Setup: use the AI teammate's setup while the AI drives:** on by default, see the next section.

## Your car's setup while the AI drives
- **If this option is OFF (or you use the default setup):** the car uses **your own setup**, the one you picked in the garage. AI Drive does not change it. In our tests we left the setup on the default, and the game's data showed the same values for your car and the AI teammate's (wings, differential, camber, toe, suspension, anti-roll bars, ride height, brake pressure and bias, tyre pressures). We did **not** test a custom setup, so we can't say if a different setup makes the AI faster or slower.
- **If this option is ON (default):** when the AI takes the car, AI Drive looks for the AI car that is the same car as yours (your teammate). **It only copies when exactly one AI car has the same car data as yours.** If several cars look the same, it can't tell your teammate from the others and **keeps your setup** (the log says so). When it does find one, it copies **these values** onto your car: differential on and off throttle, brake bias and pressure, front and rear camber, toe, anti-roll bar and ride height, and ballast. Your own values are saved first, and they come back when you take over. **The wings, tyre pressures and suspension stiffness can't be changed live, so they stay as you set them.** If your setup already matches the AI's (the default), nothing is changed. The log says exactly what was copied (`SETUP:` lines).
- **Fuel load:** the one you set before the race. **Fuel mix:** stays as you set it (Standard in our tests); in one test the menu row was greyed out. **ERS deploy mode:** the AI changes it by itself, like on the AI teammate's car (it switched between Medium and Overtake from lap to lap in our tests).

## What it does NOT do
- It does **not** make pit calls, run a qualifying programme or re-plan for a safety car (the only pit thing it does is remove the pit stop button press, see the settings above). Your car uses the pit plan the game built for it (in a test, identical to the AI teammate's).

## Recommended settings (what we tested)
- **AI Drive:** "AI drives from the start" ON, take-over countdown 3 seconds, pace 100 %, beeps ON.
- **In the game:** leave the car setup on the default and the fuel mix on Standard. Grand Prix races of 3 to 8 laps at AI difficulty up to 110.
- **With Spectator/Broadcast Mode installed:** turn "Watch: start by itself" OFF if you only want the AI to drive, otherwise it switches to the broadcast camera.
- **Launcher:** needs Swofty Launcher 1.7.0 or newer. The earlier tests of the AI part ran on 1.6.x, so 1.7 itself is **not tested yet**. If something breaks, please send the log zip.
- **Tested (on 1.6.x):** Grand Prix races of 3 to 8 laps at difficulty 110 with the AI taking the car after the start, hand-back with the countdown, taking the car again after a flashback, and one pit stop with the AI in control.
- **Not tested yet:** the setup option, the pit stop button skip in a real stop, practice, qualifying, sprint races, career and My Team, rain, red flags, a safety car, races longer than 8 laps.

## If something goes wrong
Send the log zip from the launcher (Send Logs). Please say which session type you used and what the AI difficulty was.

## For the curious (technical)
It uses Swofty's official `sessions::set_control` to switch your car's controller to the game's AI controller, with no private bindings. One small code patch is installed at load for the pit stop quick-time event: a stub in front of the block that starts the countdown, which jumps past it only for your car and only while the AI drives (otherwise the game's own instructions run). Ported from Manual Pit Lane, where it was tested.
The setup option uses Swofty 1.7's `cars::live` `get` and `set` (experimental, "not run in game yet" in Swofty's own notes). The AI car must be the only AI car whose steering lock, clutch torque, final drive, gear ratios and engine limiter are identical to yours, otherwise nothing is copied. Only values that differ are written.
When the game takes the car back (flashback, restart, garage), it waits 2.5 seconds and takes it again. At pace 100 % no hook is installed in the AI controller.
In one 5-lap race the AI Drive car burnt 1.04 kg of fuel per lap and the AI teammate 1.05 kg, with tyre wear of +3.6 % and +3.4 % per lap. This is one test, not a guarantee.

**Credits:** made by Kizi. Thanks to Swofty for the launcher, the addon system and the official game calls.

**Changelog 1.1.1:** first release as AI Drive. AI part only, rebuilt on the official control calls, waits for the race to start before taking the car, takes it again 2.5 seconds after the game took it back, skips the pit stop quick-time event while the AI drives, and uses the AI teammate's setup while the AI drives (option, on by default).
