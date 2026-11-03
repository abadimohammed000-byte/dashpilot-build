# DashPilot AI — Geode project

DashPilot is a Windows Geode mod prototype for Geometry Dash. It reads the live player state and placed level objects through Geometry Dash's native game objects, then sends jump/hold/release input through `PlayLayer::handleButton`. This is more direct and lower-latency than repeatedly capturing screenshots and sending them to a remote model.

## Compatibility

The project metadata currently targets **Geometry Dash 2.2081 + Geode 5.11.2**. Geode 5.11.2 is the latest stable release shown in the official release list; its notes mention fixes for 2.209 crashes but still say it is primarily for 2.2081. Geode 6.0.0-alpha.1 adds explicit 2.209x support and is marked as an alpha intended for developers. This project has not been migrated or tested against Geode 6 / 2.2091. The source metadata deliberately targets 2.2081, so use the matching game/loader pair or port and test it against the alpha SDK before using it on 2.2091.

## Controls and GUI

- **F6 — Arm / disarm.** Press F6 in the main menu to arm the bot; it waits until a level opens, then takes control. Press F6 during a level to take over immediately or release control.
- **F7 — Show / hide the in-level panel.** Both keybinds are configurable on the Geode mod settings page.
- **In-level panel:** bot on/off, auto-retry on/off, debug on/off, auto-start-next-level on/off, strength minus/plus.
- **Geode settings page:** saved strength slider, auto-start-on-level toggle, auto-retry toggle, debug overlay toggle, and configurable F6/F7 keybinds.

The panel displays the current state/mode, attempts and deaths, current decision reason, hazard distance, learned timing offset, planned press/hold/release state, and a 24-cell local occupancy strip. In-panel quick controls apply to the current session; settings edited in the Geode mod page are the saved preferences. Symbols are `#` solid, `!` hazard, `o` ring, `P` portal, `+` jump pad, and `.` empty.

## What the controller currently does

- Builds a local object index after level setup for solids, slopes, collision objects, hazards, rings/orbs, portals, and jump pads.
- Reads player position, vertical velocity, grounded state, gravity orientation, and current mode.
- Estimates horizontal progress per player update and uses a configurable look-ahead window.
- Applies separate heuristic rules for Cube, Ship, Ball, UFO, Wave, Robot, Spider, and Swing.
- Detects likely floor gaps when grounded and local floor collision geometry supports the estimate.
- Learns a small per-section timing bias from deaths close to a recent bot input, then retries automatically when enabled.

## Important limitations

This is an **early heuristic prototype**, not a trained model, not a full forward-physics simulator, and not a guarantee of completing levels. The mode rules are intentionally simple. Dense orb chains, tight timings, dual mode, speed/size changes, moving hazards, gravity sequences, teleport interactions, and complex memory patterns can still beat it. The floor-gap detector is a coarse geometry estimate. Start in Practice Mode and keep auto-retry off until you have checked that inputs are behaving correctly.

It does **not** connect to ChatGPT or call the ChatGPT website. A remote language model is not suitable for deciding every physics-tick input because network/API latency is much higher than the game's input window. The local controller handles immediate input; a future optional ChatGPT layer could inspect attempt summaries and suggest strategy changes, but that bridge is not included here.

## Build on Windows

This environment does not have the matching Geode SDK, Geode CLI, or Windows toolchain installed, so I could not compile or runtime-test a `.geode` binary here. The ZIP you received is the complete source project and build helper, not an installable `.geode` archive.

Requirements:

- Geometry Dash + Geode for the same target game version
- Geode CLI
- Geode SDK 5.11.2
- CMake 3.21+
- the compiler/toolchain required by your Geode SDK (typically Visual Studio Build Tools / Windows SDK)

Open PowerShell in this project folder and run:

```powershell
geode sdk install
geode sdk install-binaries
$env:GEODE_SDK
.\build.ps1
```

Or run `geode build` directly. Geode's documented build flow creates the `.geode` package in the build folder. The included script checks the CLI/SDK/CMake prerequisites, runs the build, and opens the output folder.

## Install after building

1. Open the `build` folder.
2. Find `moe.dashpilot-ai.geode`.
3. Drag that **built** `.geode` file into your Geode mods folder (or install it with the Geode CLI).
4. Launch Geometry Dash and open DashPilot AI's settings to configure the strength slider and keybinds.
5. Press F6 in the main menu. Enter a level; DashPilot should switch from **armed / waiting** to **driving**.
6. Press F7 in a level to hide/show the debug panel.

## Cloud build (no local compiler required)

Open `DashPilotCloudBuilder.html` in a browser. Create a GitHub repository initialized with a README, create a fine-grained personal access token limited to that repository with **Contents: Read and write**, **Workflows: Read and write**, and **Actions: Read** permissions, then provide the ZIP, `owner/repo`, branch, and token to the builder. The builder uploads the source plus `.github/workflows/dashpilot-build.yml`; GitHub Actions builds the Windows x64 `.geode` artifact using the official `geode-sdk/build-geode-mod` action. The token is held only in the page's memory and cleared after source upload; it is not saved by the page. If automatic artifact download is blocked by the browser, open the linked Actions run and download `dashpilot-geode-win64` manually.

For a one-time setup, create the GitHub repository with **Add a README file** enabled so it has a default branch. Enable GitHub Actions if the repository prompts you. Use the repository's default branch in the builder.
