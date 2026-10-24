# Island: a Dynamic Island for Windows

Working name: **Island**. This is a living plan, and we update it whenever a decision changes.

## 0. Current status (read this first)

- **Current phase:** Phases 2–6 are built and verified, including 5b (wide control center), 5c (auto-hide) and **6 (displays + quick toggles)**. The island hides for games and stays out of sight until you push the mouse to the top-middle; then it slides down, opens Peek on hover and the control center on click, and shrinks back up into the edge when you leave. The control center: time/date, **Microphone / Night light / Energy saver** toggles, battery, now playing, volume + per-app mixer, brightness per display with refresh rate, **projection modes (like Win+P)**, Wi-Fi/Bluetooth lists. **Next: Phase 7, "Daily-driver hardening"** (start at login, crash restart, sleep/resume, soak test), or the motion polish you pick from "Later ideas".
- Hands-on checks are left for you in the Phase 1–6 lists. Most important now: **click Night light and Energy saver once each** (no official Windows switch exists for either), mute the mic, and try a projection mode.
- ⚠ Two crashes were found and fixed on 2026-10-24 (§12): an Island crash on an empty media object, and **DWM (the Windows compositor) crashing** when a rebuilt panel released objects with springs still running. None since.
- **Last updated:** 2026-10-24 13:50 NPT (UTC+05:45)
- **Resuming in a new chat:** say "read PLAN.md".
  - This section says where things stand.
  - §11 lists every decision.
  - §12 is the update log: what changed and when, newest first.
- **Build:** run `cmake --preset default` once, then `cmake --build --preset debug` (or `release`).
  - The exe ends up in `build\Debug\Island.exe` or `build\Release\Island.exe`.
  - In VS Code, F5 closes any running island, builds the project and starts the debugger.
  - `Island.exe --quit` closes a running island. Launching a new copy replaces the old one.
- **Tests:** `ctest --preset debug` (or `release`) runs the unit tests; `build\Release\IslandTests.exe --hardware` also checks the real volume, brightness and mixer (it moves them 1 % and back) and lists the battery, Wi-Fi networks and Bluetooth devices (read only). `pwsh -File tests\fullscreen_test.ps1` runs the game-safety test with real windows (it clicks its own test windows, so hands off the mouse for ~15 s). `pwsh -File tests\visual_check.ps1` takes screenshots and measures the morphs *hands-free* (no mouse, no focus change). `pwsh -File tests\media_test.ps1` plays a silent test track to check now playing (your own music is never touched).
- **Poses:** `Island.exe --pose idle|peek|expanded|demo` holds the island in one state (for screenshots); tray → "Keep open (for tuning)" does the same while you edit settings.
- **Log and settings:** the tray menu opens `config\settings.json` (edits apply live) and the log (`%LOCALAPPDATA%\Island\island.log`).
- **Rule for every session:** after any meaningful change, add an entry at the top of the update log (§12) and refresh this section.

## 1. Goal

A small pill sits at the top-center of the screen.

- **Hover** makes it grow a little (*Peek*) to show what's playing, with a few controls.
- **Click** opens the full *control center*: media, volume, brightness, Wi-Fi, Bluetooth and connected displays.
- It should feel as smooth and glassy as Apple's UI.
- It must use little memory and power.
- It must **disappear completely** whenever a game or fullscreen video is running.

### Hard requirements (how we know it's good)

| What | Target | Hard limit |
|---|---|---|
| RAM (Task Manager "Memory" column) | 20–40 MB | **100 MB** |
| CPU while idle | 0.0 % | — |
| GPU while idle | no frames drawn | — |
| Animation smoothness | your refresh rate (144 / 165 Hz), no dropped frames | — |
| Time to hide when a game/fullscreen app starts | < 0.2 s | — |
| Times it steals focus from a game | **never** | — |

## 2. Your machine (checked 2026-10-23)

- Windows 11 build 26200, dark mode, transparency effects on.
- **Two monitors with different scaling and refresh rates:**
  - Legion 27Q-10: primary, 2560×1440 @ 144 Hz, 125 % scale.
  - Laptop panel: 2560×1600 @ 165 Hz, 175 % scale.

  Because of this, the app has to handle DPI scaling correctly from day one.
- **Two GPUs:** Intel integrated + RTX 5060 Laptop. They appear to drive different screens. Island must not keep the NVIDIA GPU awake just to draw a pill, because that drains the battery.
- Taskbar at the bottom with auto-hide on, so nothing is competing for the top edge.
- Laptop brightness control through WMI works.
- Already installed: VS Build Tools 2026 (Microsoft C++ compiler), Windows SDK 10.0.26100, CMake, Git, .NET 10, Python, Node.
- ⚠ **MinGW g++ is also on your PATH.** It cannot build this project because it lacks C++/WinRT support. We must make sure CMake uses the Microsoft compiler (MSVC).

## 3. How a Windows app actually runs (primer)

You write `.cpp` source files. The compiler (MSVC) turns them into **`Island.exe`**, a single file of about 1–2 MB. Double-clicking it runs it. There is no installer, no Store and no admin rights.

```
Island.exe starts
  └─ wWinMain()                         ← the "main" function of a Windows app
       ├─ make sure only one copy is running
       ├─ create the island window (borderless, transparent, always on top)
       ├─ hand the pill's visuals to the system compositor (GPU)
       ├─ start watchers (fullscreen/games, media, volume, Wi-Fi …)
       └─ message loop: sleep until Windows sends a message
              "mouse entered the pill"   → start the expand animation
              "fullscreen app opened"    → hide
              "monitor unplugged"        → move to the other screen
              … handle it, then go back to sleep (0 % CPU)
```

- **The compositor (DWM)** is the part of Windows that draws every window on screen. We tell it once, "grow this pill to 360 px using this spring," and DWM computes every frame itself on the GPU at 144/165 Hz. That is how we get smoothness without CPU cost. Even if our program is busy (for example, waiting on a slow monitor-brightness call), the animation does not stutter.
- There is no console window. A **tray icon** near the clock gives you Settings / Restart / Quit.
- **Start at login:** one registry value under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` points to the installed exe. It shows up in Task Manager → Startup apps, where you can turn it off. No admin rights or code signing are needed for your own PC.
- The development build lives in `build\…`. To "install," we copy it to `%LOCALAPPDATA%\Programs\Island\`. That way rebuilding never breaks the startup entry.

## 4. Tech stack: decided

**C++20 + Win32 + Windows.UI.Composition + Direct2D/DirectWrite.** We build with MSVC + CMake in VS Code.

Chosen because it uses the least RAM (2026-10-23). You've written code in C/C++, C#/Java and Python/JS before, so the syntax isn't new. What's new is the Windows-specific part.

Why this one:

- **Lightest realistic option:** about 20–40 MB idle.
- **Smoothest:** the system compositor runs the animations, using built-in spring physics (`Spring…NaturalMotionAnimation`), the same style of motion Apple uses.
- **Real glass:** the compositor can blur whatever is behind our window (the "host backdrop brush"). We can clip that blur to any shape, including a pill whose shape is animating.
- Every API we need (media, audio, radios, displays) is native to C++.
- Nothing extra to install at runtime: no .NET, no browser engine.

**Cost:** this is the hardest route. Win32 and the compositor have a learning curve. We handle that with small phases, each ending in something you can run, with each piece explained.

### Alternatives considered

| Option | Idle RAM (rough) | Smoothness | Glass pill | Difficulty | Verdict |
|---|---|---|---|---|---|
| **C++ Win32 + Composition** | 20–40 MB | best (compositor-driven) | yes, any shape | hard | ✅ recommended |
| C# + WinUI 3 | 60–100 MB | very good | awkward (transparent, shaped windows are a known WinUI 3 pain) | medium | runner-up; risks the 100 MB cap |
| C# + WPF | 60–120 MB | OK. Transparent WPF windows render on the CPU and stutter | needs undocumented hacks | easy | ❌ |
| Rust + windows-rs | 15–35 MB | best | yes | hard, with fewer examples | only if you already know Rust |
| Electron / Tauri (web UI) | 100–300 MB incl. browser engine | OK | faked | easy | ❌ RAM |

## 5. How the island behaves

```
 Hidden  ◄──── game / fullscreen app detected ────  (from any state)
   │ fullscreen gone (after a short settle delay)
   ▼
 Tucked: out of sight (auto-hide, like your taskbar)
   │ mouse touches the top edge, middle        ▲ mouse leaves ~300 ms
   ▼                                           │ (from Idle, Peek or Expanded)
 Idle: small pill ── hover ~150 ms ──►  Peek: wider, song + play/pause
                                           │ click
                                           ▼
                                     Expanded: control center
```

**Auto-hide (decided 2026-10-24, your request):** like an auto-hide taskbar, nothing shows until the mouse touches the very top edge of the screen in the middle (a 300 DIP wide spot, `layout.revealWidth`). Then the pill slides down from the edge, and the rest works as before: rest on it → Peek, click → control center. When the mouse leaves, it slides back up out of sight. Tucked away, the window is an invisible 2 px strip along the top edge; it costs nothing (no drawing, 0 CPU). `"autoHide": false` in settings.json brings back the always-visible pill (and the slim line over maximized windows).

**Where it lives (decided):** on whichever monitor Windows currently calls *primary*. It moves automatically when that changes:

- At home, docked: the Legion 27Q-10.
- At college, laptop only: the laptop's built-in panel.

There is no setting to change; it just follows the primary monitor. With a college projector, the laptop screen stays primary, so the island stays on the laptop.

**Idle look (decided):** a frosted glass pill.

**Over maximized windows (decided; only with auto-hide off):** when the window in front is maximized on the island's monitor, the pill shrinks to a slim glass line (about 4 px) so it doesn't cover tabs or title bars. With auto-hide on (the default since 2026-10-24) the island is out of sight anyway, and the pill comes down only while the mouse is on it. The line touches the very top edge of the screen. Because the cursor stops at that edge, the line is easy to hit even though it's thin. Hovering still opens it.

**Expanded panel (decided 2026-10-24: wide and short).** Idle and Peek stay small. The control center opens *wide*: up to 1180 DIP, always leaving at least 80 DIP of screen on each side (`layout.panelWidth` / `panelMargin`).

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│ 12:30                                                       🔋 77 %                 │
│ Saturday, 24 October                                        Plugged in              │
│────────────────────────────────────────────────────────────────────────────────────│
│ NOW PLAYING            │ SOUND               │ DISPLAYS       │ CONNECTIONS          │
│ ┌────┐ Title           │ Headphones          │ Legion 27Q-10  │ [Wi-Fi      ● on ]   │
│ │art │ Artist          │ 🔊 ━━━━━━━━━○──     │ ☀ ━━━━━━━━━○   │ [Bluetooth  ● on ]   │
│ └────┘ Spotify         │ Brave               │ Built-in       │  📶 Home   Connected │
│ ━━━━━━━━━━━━━━━━━━━━   │ 🔊 ━━━━━━━━○───     │ ☀ ━━━━○─────   │  📶 Cafe   Secured 🔒│
│    ⏮    ⏯    ⏭        │ System sounds  ...  │                │  ⋯ More networks  >  │
│                        │                     │                │  ⚙ Wi-Fi settings >  │
└────────────────────────────────────────────────────────────────────────────────────┘
```

- **Header:** time and date (your locale's format); three quick toggles in the middle: **Microphone** (mutes every mic; red while muted), **Night light**, **Energy saver**; battery % with its icon, and "Charging · 45 min to full" / "2 h 10 min left" / "Plugged in". Right-click a toggle for its Settings page.
- **Now playing:** artwork, title/artist/app (click the app name to switch apps), ⏮ ⏯ ⏭ and a progress bar.
- **Sound:** the main volume (caption = output device) and a **mixer**: one slider per app playing sound (up to 4 rows; an app keeps its row while it plays, so sliders never swap under the mouse). Click an icon to mute.
- **Displays:** a brightness slider per monitor, each named with its refresh rate ("Legion 27Q-10 · 144 Hz"; click the name for Settings › Display). With a second display plugged in, **Project**: PC screen only / Duplicate / Extend / Second screen only (the same as Win+P; the current one is lit).
- **Connections:** Wi-Fi and Bluetooth tiles. The switch turns the radio on/off; clicking the rest of the tile shows its list below:
  - **Wi-Fi:** networks in range (connected → saved → strongest). Click one to join. Saved and open networks connect directly. A new password-protected network opens Windows' own Wi-Fi list, because the island can't take keyboard focus to type a password (see §11).
  - **Bluetooth:** paired devices (connected → headphones/speakers → name). Click headphones or speakers to connect/disconnect. Other devices (mice, keyboards, phones) open Settings → Bluetooth, since Windows connects those itself.
  - **Add a device:** searches for new devices only while this list is shown; click one to pair (PIN confirmations appear in the row; anything that needs typing opens Windows' pairing dialog).
  - Longer lists page: the last row becomes "More networks / More devices".
- Right-click on any slider, tile or row opens the matching Settings page.

**Motion rules (the "Apple feel"):**

- Use springs, not fixed-duration easing. Width, height and corner radius morph together.
- Content crossfades: the old content fades out quickly, and the new content fades in partway through the morph with a slight scale-up.
- **Interruptible:** if you reverse mid-animation, it continues from the current position *and speed*. It never snaps. (Built: position yes; speed not yet, see Phase 3.)
- Spring values live in `settings.json`, so we can tune the feel without recompiling.

**Controls: hover and click animations (decided).** Every button, toggle, radio choice and slider animates smoothly and cleanly.

General rules:

- **Every state change animates:** rest ↔ hover ↔ pressed ↔ on/off ↔ disabled. Nothing ever jumps from one look to another.
- These animations also run in the compositor. Each control is its own GPU layer, so they stay at 144/165 Hz even when the app is busy.
- **Feedback starts the instant the mouse button goes down.** The action happens on release, so dragging off a button cancels it, as on macOS/iOS.
- **Interruptible:** moving in and out quickly never causes a snap or a stuck highlight.
- **One shared set of motion timings and springs** ("motion tokens") lives in `settings.json`, so every control feels like part of the same family.

| Control | Hover | Press / change |
|---|---|---|
| Icon button | Soft highlight circle fades in (~120 ms); icon brightens | Shrinks to ~95 % on press; springs back with a tiny overshoot on release |
| Play / Pause | Same as an icon button | The icon *morphs* play ↔ pause (crossfade with a slight scale/rotate) instead of swapping |
| Toggle switch (Wi-Fi, Bluetooth) | Track lightens slightly | Knob slides with a soft spring and the track color crossfades. The knob stretches a little while held (iOS style) |
| Radio / segmented choice (e.g. Extend / Duplicate) | Option highlight fades in | The selection pill *slides* to the new option |
| Slider (volume, brightness) | The bar grows a little (8 → 10 DIP) and its fill brightens. **No knob** (changed 2026-10-24, your request): a thick rounded bar whose filled part is the level | While dragging, it follows the cursor 1:1 with no delay, because animating here would feel laggy. Clicking a spot on the bar glides there with a spring, and changes from keyboard keys glide too |

**Window strategy (for both smoothness and clicks):**

- **Never resize the real window every frame.** Doing that is why most "island" apps stutter. The window is resized only at the start of an expand (to full panel size, while that extra area is still transparent) and again after a collapse finishes. Everything in between is GPU visuals.
- Window flags:
  - always on top
  - no taskbar or Alt+Tab entry (`WS_EX_TOOLWINDOW`)
  - **never takes focus** (`WS_EX_NOACTIVATE`)
  - transparent background (`WS_EX_NOREDIRECTIONBITMAP`)

## 6. Folder layout & architecture

```
windowsUI/
  CMakeLists.txt             Island.exe + IslandCore (shared code) + IslandTests
  PLAN.md
  src/
    main.cpp                 entry point, single-instance check, message loop
    App.*                    owns every part and wires services to the window
    app.manifest             DPI awareness (Per-Monitor V2), Windows 10/11
    AppMessages.h            "Island.Quit" message shared between Island processes
    IslandWindow.*           the window: flags, placement, DPI, hit-testing, hide/show
    StateMachine.*           Hidden / Tucked / Idle / Peek / Expanded rules (unit-tested, no window)
    render/
      IslandVisuals.*        visual tree, glass material, the morph
      Graphics.*             Direct2D/DirectWrite on the low-power GPU: text, icons, images
      Effects.*              compositor effect brushes (colour boost, tiled grain)
      Motion.*               motion tokens + spring/ease helpers
      Widgets.*              control kit: label, icon button, toggle tile, segmented, slider
      Glyphs.h               Segoe Fluent Icons characters
    views/                   what each state shows (one layer each)
      IdleView.*  PeekView.*  ControlCenter.*  ControlKitDemo.*
      Actions.h              what the views' buttons ask the app to do
      sections/              the control center's parts: header + 4 columns
        Section.h            base class + SliderRow
        StatusSection.*  NowPlayingSection.*  SoundSection.*  DisplaysSection.*  ConnectionsSection.*
    services/                ← anything slow runs on background threads
      FullscreenWatcher.*    game / fullscreen detection (event-driven)
      MediaState.*           what's playing + pure rules: which app, app names, position, artwork decoding
      WindowClassifier.*     pure rule: normal / maximized / fullscreen (unit-tested)
      MediaService.*         now playing (GlobalSystemMediaTransportControls), events -> UI thread
      AudioService.*         volume + mute of the default output and the per-app mixer (Core Audio), follows device changes
      BrightnessService.*    laptop (WMI) + external monitor (DDC/CI) on one worker thread
      Throttle.h             newest-value-wins, rate-limited hardware writes (unit-tested)
      RadioService.*         Wi-Fi / Bluetooth on/off (Windows.Devices.Radios) + Wi-Fi network name
      WifiService.*          networks in range, scan, join / disconnect (Windows.Devices.WiFi)
      BluetoothService.*     paired + nearby devices (DeviceWatcher), connect audio devices, pair
      PowerService.*         battery %, charging, time left / to full (PowerManager, Battery)
      DisplayService.*       connected displays, refresh rates, projection mode (Win+P) read and switched
      NightLightService.*    night light on/off (Windows' settings record; registry change notifications)
    platform/
      Tray.*  Startup.*  Settings.*  Log.*
  config/
    settings.json            spring values, game blocklist (read live while Island runs)
  tests/
    UnitTests.cpp            pure-logic tests, run by ctest
    fullscreen_test.ps1      game-safety test with real windows
    visual_check.ps1         hands-free screenshots + morph timing
    media_test.ps1           silent test media session
    TestHelpers.cs           backdrop, poses, screenshots for the scripts above
```

**Threading rule:** the UI thread owns the window and the compositor, and it never waits on anything slow. Services do their work in the background and post results back through the `DispatcherQueue`.

## 7. Phases

Each phase ends with something you can run and a **Done when** checklist.

### Phase 0: Setup & first window (learning phase) ✅ (2026-10-23)
- ✅ VS Code extensions **C/C++** and **CMake Tools**: already installed.
- ✅ CMake pinned to MSVC through `CMakePresets.json` (generator *Visual Studio 18 2026*, x64). `CMakeLists.txt` also refuses any non-MSVC compiler.
- ✅ The C++ runtime is linked into the exe (static), so `Island.exe` runs on its own.
- ✅ `git init` (branch `main`) and a first commit.
- ✅ An empty Win32 window plus C++/WinRT calls: it reads the Windows version and opens Settings → Display on click.
- Optional: Visual Studio Community, for its stronger debugger.
- **Done when:**
  - ✅ `Island.exe` opens a plain window and closes cleanly (exit code 0).
  - ⬜ You put a breakpoint in `wWinMain` and step through it.

### Phase 1: The pill exists ✅ built (2026-10-23)
- ✅ Borderless, transparent, topmost, no-activate tool window at the top-center of the **current primary monitor** (`IslandWindow`).
- ✅ Follows primary-monitor changes. It repositions on display, DPI and settings changes after a 250 ms settle delay.
- ✅ Per-Monitor-V2 DPI awareness through `app.manifest`.
- ✅ Glass pill drawn by the compositor (`IslandVisuals`): Windows' blur-behind brush, a dark tint (alpha 150), and a pill-shaped clip.
  - Size: 200×34 DIP with a 6 DIP gap above it. The window itself reaches the top edge, so pushing the mouse up still lands on it.
- ✅ Tray icon with a "Quit Island" menu. The tray has its own hidden window and re-adds its icon if Explorer restarts.
- ✅ Single instance: a newer launch replaces the running one, and `--quit` closes it.
- **Done when:**
  - ✅ It sits exactly at top-center at the right size. Verified on the Legion: 250×51 px at 125 %, centered (1155 px gap on each side), at the very top.
  - ⬜ (you) Unplugging the external monitor moves it to the laptop panel at the correct 175 % size, and plugging it back in moves it back.
  - ✅ Not in Alt+Tab or on the taskbar: the TOOLWINDOW style is set. ⬜ (you) Confirm with Alt+Tab.
  - ✅ Launching it doesn't steal focus. ⬜ (you) Click the pill while typing somewhere: your typing should stay where it is.
  - ✅ RAM < 30 MB: measured **1.9 MB**. The pill's visuals live in Windows' compositor (counted under `dwm.exe`), not in Island.
  - ✅ The blur works: text behind the pill is fully blurred and the edges are smooth. Windows' blur strength is fixed; Phase 3 tunes tint, saturation, brightness and the edge highlight.

### Phase 2: Stay out of the way (game safety) ✅ built (2026-10-24)
This comes before any visual polish, so you can leave it running while gaming from early on.

- ✅ Event-driven fullscreen/game detection (`FullscreenWatcher`, see §8). Nothing is polled: idle CPU measured at 0.0 ms over 20 s.
- ✅ Really hides the window (`SW_HIDE`), instantly. It comes back after `showDelayMs` (500 ms) of nothing fullscreen. Phase 3+ must keep "nothing runs while hidden" true (no animations or redraws while hidden).
- ✅ Window drags are reported (`EVENT_SYSTEM_MOVESIZESTART/END`) so hover can ignore them. Phase 3 uses this once hover exists.
- ✅ Optional exe blocklist in `settings.json`: hides whenever a listed app is the foreground app, on any monitor.
- ✅ `settings.json` with `//` comments, live reload, and safe fallbacks (a broken edit keeps the previous settings).
- ✅ A log file for events (`%LOCALAPPDATA%\Island\island.log`), and tray items to open it and the settings.
- **Done when:**
  - ✅ Automated (`tests\fullscreen_test.ps1`, all 8 pass): hides within 20 ms for a borderless fullscreen window (limit 200 ms); comes back 507 ms after it closes; stays (slim) for a maximized window; hides 12 ms after the front window switches to fullscreen like F11; stays for fullscreen on the laptop screen; stays hidden over a fullscreen window while you click on the other monitor. Blocklist checked by hand: hid at once, back after 502 ms.
  - ✅ 32 unit checks of the fullscreen rule and settings parsing pass.
  - ⬜ (you) The real-game rows of the matrix in §10: Valorant, Marvel Rivals, YouTube fullscreen, VLC, plus PresentMon showing *Independent Flip* with Island running.

### Phase 3: Motion & glass (the feel) ✅ built (2026-10-24)
- ✅ Visual tree, rounded-rectangle clip, and one spring morph for width, height, top gap and corner radius (Idle ↔ Peek ↔ Expanded). The pill is centered *relative* to the window, so growing the window at the start of a morph never moves it.
- ✅ The window is resized only at the start of a morph (to the target plus ~5 % room for overshoot) and once the spring has settled (to fit exactly). Never per frame.
- ✅ Hover intent (150 ms), leave delay (300 ms Peek, 500 ms control center) and hysteresis (the Peek window is bigger than the idle pill). Hover is ignored while a window is being dragged, and after a drag that ends on the island until the mouse leaves.
- ✅ Slim line over maximized windows (150×4 DIP touching the top edge), morphing to and from the pill.
- ✅ **Control kit** (`render/Widgets.*`): icon button (hover circle, press shrink with overshoot, play ↔ pause morph), toggle tile with a switch (knob spring, colour crossfade, knob stretch while held, "working" pulse), segmented choice (sliding selection), slider (1:1 drag, glide on click/wheel, thicker track and bigger thumb on hover, ignores outside changes while dragging), labels. Shown on the **control kit demo** panel (tray → "Control kit demo"); until Phase 4 a click on the island opens it too.
- ✅ Glass: host backdrop blur → colour-matrix saturation boost (1.6) → dark tint (0.55) → tiled grain (3.5 %) → gradient rim hairline. Solid dark fallback when Windows has transparency effects off (follows the setting live).
- ✅ Text with DirectWrite (Segoe UI Variable), icons from Segoe Fluent Icons, drawn on the **Intel** GPU (low-power adapter; confirmed in the log).
- ✅ Every size, spring, fade and the glass come from `settings.json` and reload live. "On" colour: your accent, or blue when the accent is grey (yours is #757575, so it's blue); `glass.accent` overrides it.
- **Done when:**
  - ✅ Measured at 144 Hz (`visual_check.ps1`): every morph reaches its target within ~200–225 ms with a 2–4 px (~2 %) overshoot. ⬜ (you) Does it *feel* right on both screens (144 and 165 Hz)? Tune `motion` in settings.json.
  - ✅ Interrupting never snaps: a morph reversed after 120 ms turns around from its current width. ⚠ It restarts from rest, though, instead of carrying its speed (the compositor's springs don't keep velocity). See "Later ideas".
  - ✅ Idle CPU 0.0 ms over 20 s; **RAM 25 MB** (Task Manager "Memory"); handle count steady.
  - ✅ 93 unit checks pass, incl. the state machine (found and fixed a stale-hover-timer bug) and the pointer routing / slider rules.
  - ⬜ (you) Hover, click, drag every control in the demo; check the slim line over a maximized browser.

### Phase 4: Now playing ✅ built (2026-10-24)
- ✅ `MediaService` listens to Windows' media sessions (the same source as the volume flyout). Events arrive on background threads and are handed to the UI thread. Each "sessions changed" notice gets one follow-up look 1 s later, and every open of the island re-syncs everything (E4).
- ✅ Which app to show (`PickSession`, unit-tested): your manual choice → one that is playing (the one you most recently pressed play in) → the one already shown (paused apps don't take turns as Windows reshuffles them) → Windows' own choice. Metadata updates don't count as activity, because browsers send them constantly.
- ✅ Idle pill: tiny artwork + a still "sound bars" mark (dimmed while paused). Peek: artwork, title, "artist · app", play/pause + next. Control center: 64 DIP artwork, title, artist, app name (click to switch when several apps have media, E1), previous/play-pause/next, progress line.
- ✅ Artwork decoded off the UI thread to 192 px, the middle square cut out of 16:9 thumbnails, matched to the song (late artwork for an old song is dropped, E2), crossfaded in; placeholder note while loading.
- ✅ The compositor glides the progress line linearly to the end of the song. It only runs while the panel is visible (no frames in idle), and it's hidden when the app gives no timeline (E3).
- ✅ Play/pause flips the icon at once (play ↔ pause morph); the real state is re-read 1.5 s later in case the app ignored it.
- **Done when:**
  - ✅ Spotify: your real session shows title, artist, artwork and the paused state. ✅ Brave (Chromium) is detected and followed. ⬜ (you) Chrome/Edge YouTube and Media Player.
  - ✅ Switching: a silent test player (`media_test.ps1`) takes over within ~0.1 s of starting to play (~0.5–0.7 s including starting the player); on pause the island moves to the app that's really playing; when it closes it moves on.
  - ✅ RAM 31 MB with Spotify + Brave sessions, idle CPU 0.0 ms over 20 s; 119 unit checks pass (incl. the artwork crop). ⬜ (later, Phase 7 soak) memory flat after an hour of track changes.

### Phase 5: Quick controls ✅ built (2026-10-24)
- ✅ **Volume** slider (caption = output device, e.g. "Headphones") and mute on the speaker icon (Core Audio). It follows default-device changes (E6). Our own changes are tagged, so their echoes are ignored (D9); changes from anywhere else glide the slider. Raising the volume unmutes, like Windows' slider.
- ✅ **Brightness**, one slider per display, the island's monitor first: the laptop panel via WMI, the Legion 27Q-10 via DDC/CI (low-level VCP 0x10). One worker thread; only the newest value is sent; at most 4 writes/s for DDC/CI and 10/s for WMI, and the final value always lands (`Throttle`, E7/E9). A monitor that doesn't answer gets a disabled slider and "turn on DDC/CI in its menu" (E8). Values are re-read when the panel opens and when displays change (monitors can't announce changes), and the panel rebuilds when a display comes or goes.
- ✅ **Wi-Fi and Bluetooth** tiles: a click flips at once with a "working" pulse; clicks during switching are merged; it springs back if Windows refuses (D8). They show the real state ("Off in Windows" for airplane mode / disabled adapters, E11), and the Wi-Fi network name ("Connected" if Windows hides it, E10). Right-click opens the Settings page (also for the sliders: sound / display).
- **Done when:**
  - ✅ Built and running on your machine: Headphones, Legion 27Q-10 (DDC/CI works), built-in display (WMI works), Wi-Fi with its network name, Bluetooth.
  - ✅ `IslandTests --hardware`: a volume change made outside Island reaches it; brightness writes reach both displays (set 99 %/51 %, read back the same), all restored afterwards.
  - ✅ 135 checks pass (incl. `Throttle`); RAM 33.7 MB, idle CPU 0.0 ms over 30 s, handles steady. Launching Island 5× after the fix: no DWM crash (before: 4 of 7).
  - ⬜ (you) The sliders *feel* instant while dragging (brightness hardware lags behind on purpose); volume keys and Quick Settings move the island's sliders; toggle Wi-Fi/Bluetooth (I didn't, since that would have cut your connection).

### Phase 5b: Wide control center ✅ built (2026-10-24, your redesign request)
You asked for: a wide, horizontal control center (with a margin on both sides) instead of the tall square one; the Wi-Fi networks to switch between; Bluetooth devices to pair, connect and disconnect; date, time and battery; and a per-app volume mixer. Idle and Peek were already right and are unchanged.

- ✅ **Layout** (§5): header (time, date, battery) over four columns: Now playing · Sound · Displays · Connections. Up to 1180 DIP wide, at least 80 DIP from each screen edge, 30 DIP corners. On your Legion it is 1476×486 px (was 400 DIP wide and taller than wide).
- ✅ **Status** (`StatusSection`, `PowerService`): clock in your locale's format, updated once a minute only while the panel is open; battery % and icon; "Charging · 45 min to full", "2 h 10 min left", "Plugged in" (yours: 77 %, plugged in, not charging, which is likely Lenovo's battery-saving charge limit), "· Energy saver".
- ✅ **Mixer** (`AudioService`, `SoundSection`): one slider per app with sound on the current output (an app's sessions merged; games' `-Win64-Shipping` names cleaned up; "System sounds" last). Click the icon to mute an app. Up to 4 apps; rows stay put while apps come and go.
- ✅ **Wi-Fi list** (`WifiService`): networks in range, one row per name with the strongest signal; scans when the list opens and every 10 s while it's on screen (never while closed). Click to join (see §5 for passwords).
- ✅ **Bluetooth list** (`BluetoothService`): paired devices from an always-on, event-driven device watcher; headphones/speakers connect and disconnect from the island (the same "reconnect"/"disconnect" request Windows' own Bluetooth page sends to the audio driver); "Add a device" searches only while shown and pairs with a click.
- ✅ Wi-Fi/Bluetooth tiles sit side by side: the round icon turns the radio on/off (like macOS), the tile shows its list. Long lists page with a "More…" row.
- **Done when:**
  - ✅ Release and Debug build with no warnings; 162 unit checks pass (new: Wi-Fi grouping, mixer grouping and row assignment, game exe names, Bluetooth device kinds and order, list paging, battery text and icon).
  - ✅ `IslandTests --hardware` (171 checks): Brave's mixer volume set to 99 % and read back by a fresh reader, then restored; Wi-Fi scan: 9 networks; 7 paired Bluetooth devices, 3 that Island can connect (Living Room speaker, Galaxy Buds, Galaxy Watch). Nothing was switched, joined, connected or paired.
  - ✅ Hands-free screenshots: the full panel, the Wi-Fi list (with "Looking for networks…", then 6 networks paged 1–3), the Bluetooth list (Living Room speaker connected, AirPods, Galaxy Buds, "More devices 1–3 of 7", "Add a device").
  - ✅ RAM 32.9 MB (Task Manager "Memory"); handles steady (547 → 547 over 30 s). Idle CPU: 0–94 ms per 2 minutes (one 15.6 ms scheduler tick at a time, ≤ 0.08 % of one core; Task Manager shows 0 %), on a Windows thread-pool thread. None of Island's own threads run, and no service reports anything while idle. Phase 5 measured a flat 0.0; the difference is Windows-side (to recheck in the Phase 7 soak).
  - ⬜ (you) Join another Wi-Fi network, connect/disconnect your Galaxy Buds or AirPods, pair something new, drag an app's slider in the mixer while it plays. I didn't, because it would have cut your connection or your audio.

### Phase 5c: Auto-hide, knob-less sliders, lighter glass ✅ built (2026-10-24, your request)
- ✅ **Auto-hide** (§5): new state *Tucked* in `StateMachine`; the window is an invisible 2 px × 300 DIP strip at the top edge; touching it slides the pill down; leaving slides it back up (300 ms delay). Hidden ↔ Tucked for fullscreen apps works as before.
- ✅ **Sliders:** no knob; 8 DIP bar (10 DIP when hovered or held) whose filled part is the level; even pixel sizes so the bar sits centred on whole pixels at 125 % and 175 %.
- ✅ **Glass:** tint 0.55 → 0.42.
- **Done when:**
  - ✅ Release and Debug build with no warnings; 178 unit checks (new: the auto-hide rules: reveal on touch, Peek after resting, straight back to tucked from Peek/the control center, tucked again after leaving the pill, no reveal while dragging a window, fullscreen → hidden → tucked, switching the setting live, poses).
  - ✅ Hands-free (`visual_check.ps1`, which now also "touches" the strip with a posted mouse message, never your mouse): tucked = 376×2 px strip, nothing visible; touch → pill visible after ~90 ms; mouse gone → tucked 307 ms later, window back to the strip ~190 ms after that. Screenshots of the new sliders and the lighter glass.
  - ✅ Tucked: 0.0 ms CPU over 60 s, 32.8 MB RAM; no crashes in the event log.
  - ⬜ (you) Does pushing the mouse to the top-middle feel right? Is 300 DIP wide enough or too wide (`layout.revealWidth`)? Is the glass light enough (`glass.tintOpacity`, lower = clearer)? `tests\fullscreen_test.ps1` wasn't re-run (it moves your mouse; the fullscreen rules didn't change, and the unit tests cover hidden ↔ tucked).

### Phase 6: Displays (+ quick toggles) ✅ built (2026-10-24)
- ✅ Displays are listed by friendly name with their refresh rate; clicking a name opens `ms-settings:display`.
- ✅ **Projection** (like Win+P), shown once a second display is plugged in: PC screen only / Duplicate / Extend / Second screen only, read with `QueryDisplayConfig` and switched with `SetDisplayConfig` on a background thread (the same documented calls Win+P uses). The current mode is lit; the chosen one pulses until Windows has switched.
- ✅ Re-read on every display change (`WM_DISPLAYCHANGE`, after the 250 ms settle), and the column is rebuilt when the projection list appears or goes.
- ✅ Also in this round (your request): header toggles for **Microphone** (Core Audio: mutes every active mic, follows the default mic), **Night light** (E18), **Energy saver** (E19); the control center now **shrinks and slides up into the top edge** when it closes with auto-hide, and fades only at the very end.
- **Done when:**
  - ✅ Release and Debug build with no warnings; 211 unit checks (new: night light record read/switch on your two real records, damaged records left alone; energy saver plan; projection mapping; refresh rate; display captions).
  - ✅ `--hardware` (222 checks, all read-only for the new parts): night light on (record understood), microphone "Microphone Array", energy saver off (newer status, correct while plugged in), 2 displays connected in Extend, 2560×1600 @ 165 Hz and 2560×1440 @ 144 Hz.
  - ✅ Hands-free screenshot of the new header and Displays column; closing timeline: the panel shrinks from 1452 to ~570 px in ~140 ms and slides up out of sight (before: it faded out in place). Tucked: 0.0 ms CPU over 60 s, 33.5 MB; no crashes.
  - ⬜ (you) **Click Night light and Energy saver once each**: neither has an official Windows switch, and I didn't switch them for you (it would have changed your screen/power). If one does nothing, tell me. Energy saver puts itself back and opens Settings if Windows doesn't follow within 4 s.
  - ⬜ (you) Mute/unmute the mic; try a projection mode (your Legion goes dark for "PC screen only"; click Extend on the laptop screen to get it back); unplug the Legion and check the Project list disappears.

### Phase 7: Daily-driver hardening
- Start at login (Run key) plus an install script.
- `settings.json` with safe defaults. A corrupt file falls back to defaults instead of crashing.
- Crash log plus automatic restart.
- Correct state after sleep/resume, lock/unlock and GPU resets.
- **48-hour soak test:** the memory graph stays flat and the handle count doesn't grow.
- Polish: Island's own tray icon (currently Windows' generic app icon) and a dark-mode tray menu.
- **Done when:** it runs for a week without you thinking about it.

### Later ideas (not committed)
- **Motion polish candidates** (discussed 2026-10-24, waiting for your pick): content that slides with the morph, staggered entrances, a hover highlight that glides between list rows, press feedback from the click point, a slider that grows while dragging, value bubbles on sliders, a lit edge that follows the mouse, toggle colour bloom, rolling numbers for percentages, a soft glow while something plays. See the chat for details.
- Lighter blur: Windows' blur strength can't be changed; a capture-based glass with our own blur would cost GPU time and battery (and needs screen capture), so it's parked.
- Carry the current speed into a reversed morph (compute the spring's velocity ourselves and pass it as `InitialVelocity`). Positions are already continuous.
- Replace Windows' own volume/brightness popup with the island. Hiding the built-in one is hacky and fragile.
- Notifications, timer/stopwatch, charging animation.
- A **"Liquid Glass" refraction** experiment. True refraction needs screen capture plus a custom shader, which costs GPU, so we'd only do it if it fits the budget.

## 8. Game safety: how we stay out of games' way

**Rules (never broken):**

1. **We're a normal window, nothing more.** No DLL injection, no hooking other processes, no reading game memory, no drawing inside the game, no drivers. This puts us in the same category as everyday desktop apps. Nobody can promise what an anti-cheat will do, but we avoid everything they look for.
2. **No global keyboard/mouse hooks** (`WH_MOUSE_LL` / `WH_KEYBOARD_LL`). They add delay to *every* mouse movement system-wide, which is terrible in Valorant, and anti-cheats watch for them. We detect hover only on our own window (`TrackMouseEvent`).
3. **Never open handles to game processes.** Anti-cheat-protected games dislike it. We identify apps by window info and process-list names instead.
4. **Never take focus.** That means `WS_EX_NOACTIVATE` and `MA_NOACTIVATE`, and we never call `SetForegroundWindow`. Clicking the island must never minimize a game.
5. **When hidden, be truly gone.** That means `SW_HIDE`, all animations and timers stopped, and no frames drawn. A *transparent* window on top of a borderless game still forces Windows to compose it. That knocks the game out of "independent flip" mode, which costs input lag and FPS. Invisible isn't enough.

**Detection (layered, event-driven, no busy polling):**

| Signal | Catches |
|---|---|
| AppBar `ABN_FULLSCREENAPP` notification (the same signal the taskbar uses to hide itself). Used as a trigger to re-check, because on two monitors it doesn't say which screen | most fullscreen apps and games |
| Foreground-window change events (`SetWinEventHook`, out-of-context, so no injection) → check whether that window covers its whole monitor | borderless-windowed games, F11 browser, fullscreen video |
| The top app window *under the island's spot* (a z-order walk that skips always-on-top, tool and click-through windows, i.e. overlays) | a fullscreen video or game stays "in front" while you click something on the other monitor |
| Move/resize/hide/close events, but only from the threads of those two windows. Tested: mouse-pointer moves are reported by a system thread, so these hooks stay quiet while you play | F11 / fullscreen-button switches with no focus change |
| `SHQueryUserNotificationState` | exclusive-fullscreen D3D, presentation mode |
| Optional exe blocklist, as an escape hatch | any game the checks above ever miss |

**Policy (decided): hide exactly like the taskbar does.**

- If anything covers the **whole primary screen**, the island hides. That includes:
  - a game in *Fullscreen* or *Windowed Fullscreen* (borderless)
  - fullscreen YouTube or video
  - an F11 browser
- A game in plain *Windowed* mode is a normal window, so the island stays. If that window is maximized, the island shrinks to the slim line.
- Fullscreen apps on the *other* monitor don't affect it, because the island isn't on that screen and can't overlap them.
- No per-game list is needed; detection is generic.
- Hiding is instant. Coming back waits about 0.5 s, so alt-tabbing and loading screens don't make it flicker.

**Verification:** PresentMon (a free Intel tool) shows how each game presents frames. With Island running, the game must stay on *Hardware: Independent Flip*.

**How a window is classified** (`WindowClassifier`): *fullscreen* = covers the whole monitor and is not an ordinary maximized window with a title bar or resize border; *maximized* = maximized on the island's monitor. With your auto-hide taskbar a maximized window also covers the whole monitor, so the frame style is what tells them apart.

## 9. Bug & risk list

### A. Games & fullscreen
| # | What could go wrong | Why | Prevention |
|---|---|---|---|
| A1 | Island shows over a game | Borderless games don't always announce "fullscreen" | Layered detection + blocklist + test matrix |
| A2 | Lower FPS or more input lag even though the island is "invisible" | Any window overlapping the game breaks independent flip | Really hide with `SW_HIDE`; verify with PresentMon |
| A3 | Clicking the island minimizes the game or alt-tabs you out | Focus stealing | `WS_EX_NOACTIVATE`; never activate |
| A4 | Mouse feels laggy in shooters | Global low-level mouse hook | Never use one |
| A5 | Anti-cheat flags the app | Injection, hooks, handles to the game | We use none of these |
| A6 | Island pops open mid-game when the cursor hits the top edge | Hover reacts during a game | Hidden state ignores hover; hover needs dwell time |
| A7 | Island uses GPU/CPU while you game | Background timers or animations | Everything paused while hidden; no render loop ever |
| A8 | Island never comes back after the game closes | A missed "fullscreen ended" event | Re-check on foreground change, fullscreen-ended and window-destroyed events |
| A9 | Island flickers during loading screens or alt-tab | Fullscreen state toggling rapidly | Instant hide, delayed show |

### B. Everyday desktop
| # | What could go wrong | Why | Prevention |
|---|---|---|---|
| B1 | An invisible box around the pill blocks clicks on browser tabs | Window bigger than the pill | Window matches the pill when idle and grows only during expand |
| B2 | Pill covers part of a maximized browser's tab strip | It sits on the same pixels | **Decided:** shrink to a slim line while a maximized window is in front. Remaining trade-off: the line still owns the top ~4 px at center, so a click there opens the island instead of a tab |
| B3 | It expands while you drag a window to the top (Snap Layouts) | Drag passes over the pill | Ignore hover during move/resize (`EVENT_SYSTEM_MOVESIZESTART/END`) |
| B4 | Expand/collapse loops and flickers at the edge | Shrinking moves the edge out from under the cursor | Hysteresis + leave delay |
| B5 | Fights with other always-on-top windows (Task Manager, PiP video, Discord) | Topmost "wars" | Re-assert topmost only on our own state changes, never in a loop |
| B6 | Shows up in Alt+Tab or the taskbar | Wrong window style | `WS_EX_TOOLWINDOW` |
| B7 | Tray icon vanishes or island gets stuck after Explorer restarts | Shell state is lost | Handle the `TaskbarCreated` message |
| B8 | Missing on other virtual desktops | Virtual desktop rules | Test; add handling if needed |
| B9 | With auto-hide, a click at the very top of the screen in the middle (e.g. on a maximized browser's tab) brings out the island instead | The invisible strip owns the top 2 px there, like an auto-hide taskbar's strip at the bottom | Only 2 px tall and 300 DIP wide (`revealWidth`); after tucking away, the window drops back to the strip 180 ms later (once faded out), not when the spring settles (~1.5 s) |
| B10 | Auto-hide never reveals the island | A monitor placed *above* the primary one: the mouse doesn't stop at the top edge, it passes onto the other screen | Not an issue for your setup (side by side); `"autoHide": false` as the fallback |

### C. Displays, DPI, GPU
| # | What could go wrong | Why | Prevention |
|---|---|---|---|
| C1 | Blurry or wrong size on the laptop screen (175 %) vs. the external one (125 %) | DPI scaling | Per-Monitor-V2 awareness, `WM_DPICHANGED` |
| C2 | Lost, or on the wrong screen, after unplugging a monitor, closing the lid, or switching between home and college | Stale position, or the primary monitor changed | Reposition on `WM_DISPLAYCHANGE` / work-area changes. Short settle delay, because Windows reports in-between states while docking |
| C3 | Animation stutters at 144/165 Hz | Timer-driven animation or moving the window per frame | Compositor animations only |
| C4 | Island goes black or disappears after sleep, a driver update or a GPU reset | GPU "device lost" | Detect it and rebuild GPU resources |
| C5 | Keeps the NVIDIA GPU awake and drains the battery | Graphics device created on the dGPU | Use the low-power GPU; check NVIDIA's GPU-activity icon |
| C6 | Glass looks washed out with HDR on | SDR→HDR mapping | Test and adjust tint |

### D. Glass & visuals
| # | What could go wrong | Why | Prevention |
|---|---|---|---|
| D1 | Blur disappears if Windows' "Transparency effects" setting is off, or over Remote Desktop | Windows disables blur for every app in these cases | A plain dark tinted pill as a last resort. You control that setting, so normally you'll never see this |
| D2 | Windows' built-in acrylic goes solid when the window isn't focused, and ours never is | That's how DWM's backdrop behaves | Use the compositor backdrop brush (`DWMWA_USE_HOSTBACKDROPBRUSH`), not DWM's backdrop |
| D3 | Text unreadable over a white web page | Glass over a bright background | Dark-biased tint + saturation + subtle text shadow; test over white and black |
| D4 | Visible banding in the blur | 8-bit gradients | Fine noise texture |
| D5 | Glass costs a tiny bit of GPU when something moving is behind it (e.g. a video under the pill) | Windows re-blurs that small area whenever it changes, the same way it does for the Start menu and taskbar | Negligible for a pill this size. Confirm in Phase 3 |
| D6 | Energy Saver (the low-battery mode, not just being unplugged) *might* turn off blur for every app | Not known until we test it | **Decided:** one look on battery and plugged in. If Energy Saver turns off blur for every app, no app can force it back on, and the D1 fallback appears only while Energy Saver is on |
| D7 | A hover highlight stays "stuck" after the mouse leaves quickly | A missed mouse-leave message | Re-arm leave tracking on every enter; clear all hover states on collapse/hide |
| D8 | Toggles feel slow, because Wi-Fi/Bluetooth take 1–2 s to actually switch | The hardware is slow | The toggle flips instantly and shows a subtle "working" shimmer. It springs back only if Windows refuses. Rapid clicks are merged so the radio doesn't flip on/off/on |
| D9 | A slider jitters while you drag it | Windows reports our own volume change back to us, and that fights the drag | Ignore these echoes while dragging |
| D10 | Text looks blurry during press animations | Scaling a drawn image blurs it | Keep the press scale subtle (about 5 %); text is always crisp at rest |
| D11 | **DWM (Windows' compositor) crashes, and the whole screen goes black for a few seconds** | A Windows bug in `dwmcore.dll` (`CNaturalAnimation::SampleInitialValueAndFinalValueIfNecessary`): a spring animation whose target object is released before DWM has read its start value. Happened when the control center was rebuilt right after hidden sliders/toggles had started springs | **Hidden content never animates** (views pass "am I visible?" to every update); a rebuilt view's old layer is hidden and released only after 3 s (`View::Retire`). Fixed and verified 2026-10-24 |

### E. Features
| # | What could go wrong | Why | Prevention |
|---|---|---|---|
| E1 | Wrong song shown (Spotify and YouTube both open) | Several media sessions | Priority: playing > most recently changed; manual switch |
| E2 | Artwork missing, late, or from the previous song | Artwork arrives asynchronously | Match artwork to the track; placeholder while loading |
| E3 | Progress bar wrong for some apps | Some apps don't report a timeline | Hide the bar when there's no timeline |
| E4 | Media updates stop after a while | Windows media events are sometimes flaky | Resync on session change and whenever the panel opens |
| E5 | Random crashes | UI touched from a background thread | Every callback is marshaled to the UI thread |
| E12 | Island crashes while an app's media session starts or ends | Windows briefly returns *empty* objects (e.g. `GetPlaybackInfo()`), and calling a method on one is a crash, not a catchable error | Every object from the media and network APIs is null-checked. Found from a crash dump on 2026-10-24 |
| E6 | Volume slider still controls the old device after plugging in headphones | Default device changed | Listen for default-device changes |
| E7 | Brightness slider feels laggy | WMI takes 50–500 ms and DDC/CI 50–200 ms per call | Background thread, send only the latest value, rate-limit |
| E8 | External brightness doesn't work | DDC/CI is off in the monitor's menu, or unsupported | Detect it, hide the slider, show a hint |
| E9 | Constant writes wear the monitor's settings memory | DDC/CI writes are stored in the monitor | A few writes per second at most, plus the final value on release |
| E10 | Wi-Fi name shows blank | Windows 11 24H2+ needs Location permission to read Wi-Fi names | Show "Connected" when permission is denied |
| E11 | Bluetooth toggle missing or wrong | Adapter disabled, or airplane mode on | Show the real state, never a fake one |
| E13 | Bluetooth list slow to fill | On your PC Windows takes exactly 30 s to say a Bluetooth device list is *complete* (every query, even the simplest) | Devices are shown as they arrive (all 9 within 1 s); nothing waits for "complete". Audio-device lookups are merged so a burst of devices costs one lookup |
| E14 | Can't type a Wi-Fi password into the island | The island never takes keyboard focus (it would steal it from games) | Saved and open networks join directly; new secured networks open Windows' own Wi-Fi list (`ms-availablenetworks:`) |
| E15 | "Connect" does nothing for a mouse or phone | Windows has no public "connect" call for Bluetooth; Island sends the audio driver's reconnect/disconnect request, which only headphones and speakers have | Only audio devices get connect/disconnect; others open Settings → Bluetooth |
| E16 | Mixer sliders swap places under the mouse | Apps start and stop sound all the time | An app keeps its row while it has a session; rows aren't reassigned during a drag |
| E18 | Night light button does nothing, or stops working after a Windows update | No public API: Island edits the same small settings record Quick Settings writes (`bluelightreductionstate` and its per-device copy) | The record is checked byte by byte first; one that doesn't look right is never written, and the button opens Settings instead. Registry change notifications keep the button in step with Quick Settings and the schedule |
| E19 | Energy saver button doesn't switch | No public "on now" call | Island sets the documented "start at battery level" power setting to 100 % (= always, like Settings' "Always use energy saver") and puts your own levels back when you turn it off (kept in `HKCU\Software\Island`, so it survives a restart). If Windows hasn't switched within 4 s, everything is put back and Settings opens |
| E20 | A projection click blanks a screen | "PC screen only" / "Second screen only" turn a display off, like Win+P | Same as Win+P; the island follows to the screen that stays on, where Extend brings the other back |
| E17 | An app vanishes from the mixer after pausing | Browsers close their audio stream a while after a video pauses (Windows' own mixer drops it too) | Expected; it comes back when the app plays again |

### F. Lifecycle & reliability
| # | What could go wrong | Why | Prevention |
|---|---|---|---|
| F1 | Two islands on screen | Launched twice | Single-instance mutex |
| F2 | Island silently dies and stays gone until reboot | A crash | Crash log + auto-restart |
| F3 | Broken at login | Starts before Explorer/taskbar is ready | Wait and retry |
| F4 | RAM slowly grows over days | Unregistered event handlers, unreleased COM objects, old artwork | RAII and smart pointers; 48 h soak test |
| F5 | Battery drain while idle | Fast timers waking the CPU | Event-driven only; check Task Manager "Power usage" |
| F6 | Stale info after sleep | Nothing refreshes on resume | Refresh everything on resume |
| F7 | Rebuilding breaks start-at-login | Startup entry points into the build folder | Startup points at the *installed* copy |
| F8 | Confusing build errors | MinGW g++ gets picked instead of MSVC | Pin the CMake kit to MSVC |

## 10. Testing

**Game / fullscreen matrix** (run it at the end of Phase 2 and again before every "release"). `tests\fullscreen_test.ps1` automates the generic rows with real test windows; the game rows need you:

| Scenario | Expected |
|---|---|
| Valorant: Windowed Fullscreen *and* Fullscreen, on each monitor | Hidden; PresentMon shows Independent Flip |
| Marvel Rivals: borderless and fullscreen | Hidden |
| A story game in exclusive fullscreen; alt-tab out and back | Hidden in the game, back on the desktop, no flicker |
| YouTube fullscreen in Chrome/Edge; F11 browser, on the primary screen | Hidden |
| Valorant in plain Windowed mode | Island stays; slim line if the window is maximized |
| VLC / Media Player fullscreen | Hidden |
| Maximized (not fullscreen) window in front | Slim line; hovering opens the island |
| Unplug the external monitor (go to college) / plug it back in | Island moves to the new primary, correct size and sharpness |
| On battery, then with Energy Saver on | Same glass look (fallback only if Energy Saver turns blur off) |
| Game fullscreen on the laptop screen while the island is on the Legion | Island stays on the Legion (it doesn't overlap the game) |
| Lock, sleep and resume while a game is running | Correct state afterwards |

**Measured every phase:**

- Task Manager: Memory, CPU, GPU, Power usage.
- Process Explorer: handle count, for leaks.
- PresentMon: game present mode.

Note: some of the GPU memory used to draw our visuals is counted under `dwm.exe`, not Island. That's normal.

## 11. Decisions log

| Date | Decision |
|---|---|
| 2026-10-23 | Language: C++ (Win32 + Composition), chosen for lowest RAM |
| 2026-10-23 | Idle look: frosted glass pill |
| 2026-10-23 | Over maximized windows: shrink to a slim line |
| 2026-10-23 | Monitor: always the current primary (Legion 27Q-10 at home, laptop panel at college, laptop stays primary with a projector) |
| 2026-10-23 | One glass look on battery and plugged in; no power-based switching |
| 2026-10-23 | Hide like the taskbar: anything covering the whole primary screen hides the island. No per-game list needed |
| 2026-10-23 | Every hover, click and toggle is animated smoothly (§5, Controls); the control kit is polished in Phase 3 before any feature uses it |
| 2026-10-23 | Build setup: CMake presets + Visual Studio 2026 generator (MSVC only), static C++ runtime, C++/WinRT headers from the Windows SDK (no NuGet) |
| 2026-10-23 | A newer Island launch replaces the running one (instead of refusing to start); `--quit` closes it. F5 runs `--quit` before building, because a running exe can't be overwritten |
| 2026-10-23 | The tray menu uses its own hidden window, which may be activated briefly when you click the tray icon. The island window itself is never activated |
| 2026-10-23 | Idle pill: 200×34 DIP, floating 6 DIP below the top edge; the window reaches the top edge, so the mouse can find it by pushing up |
| 2026-10-23 | PLAN.md keeps a status section (§0) and an update log (§12); `CLAUDE.md` tells every new chat to read it and keep it current |
| 2026-10-24 | Fullscreen detection looks at the foreground window *and* the top app window under the island's spot (overlays skipped). The taskbar's `ABN_FULLSCREENAPP` only triggers a re-check, since on two monitors it doesn't say which screen |
| 2026-10-24 | The blocklist hides the island whenever a listed exe is the foreground app, on either monitor (it's an escape hatch, so it errs on hiding) |
| 2026-10-24 | `settings.json` is read from next to `Island.exe`, else from the repo's `config\` folder (builds made from the repo). `//` comments allowed; edits apply live; a broken edit keeps the previous settings |
| 2026-10-24 | A small event log at `%LOCALAPPDATA%\Island\island.log`, rewritten at each start and capped at 2 MB |
| 2026-10-24 | Code is split into `IslandCore` (a static library) + `Island.exe`, so `IslandTests` can test the same code; tests run with `ctest` |
| 2026-10-24 | The island morph is one spring (damping 0.78, period 65 ms ≈ 200–225 ms to settle visibly); the window grows at the start of a morph and shrinks to fit once the spring has finished |
| 2026-10-24 | "On" colour = the Windows accent, unless it's grey (then blue `#409CFF`); overridable with `glass.accent` |
| 2026-10-24 | `--pose <state>` and tray → "Keep open (for tuning)" pin the island in one state. Visual tests use poses, so they never move your mouse or take focus |
| 2026-10-24 | The control kit demo panel stays available from the tray after Phase 4/5 replace it as the click target |
| 2026-10-24 | Media: "activity" = starting to play (not metadata updates). When nothing plays, keep the app already shown. The app name in the control center switches apps when several have media |
| 2026-10-24 | The progress line is a compositor glide that only runs while the panel is visible; no elapsed/remaining times (they'd need a redraw every second) |
| 2026-10-24 | Hidden content never animates, and replaced visual layers are released 3 s late (works around the DWM crash D11). Every new view must follow this |
| 2026-10-24 | Brightness: DDC/CI through the low-level VCP calls (no slow capabilities query), max 4 writes/s; WMI for the laptop panel, max 10/s; both re-read when the panel opens instead of listening to WMI events |
| 2026-10-24 | Volume slider shows 0 while muted; dragging it up unmutes (like Windows) |
| 2026-10-24 | Tests never click anything but their own windows; visual and media tests are hands-free (poses, silent test player) |
| 2026-10-24 | **Control center is wide and short** (your request): up to 1180 DIP, ≥ 80 DIP from each screen edge (`layout.panelWidth`, `panelMargin`), header + 4 columns. Idle and Peek unchanged |
| 2026-10-24 | Wi-Fi passwords are never typed into the island (no keyboard focus, ever): new secured networks open Windows' Wi-Fi list |
| 2026-10-24 | Bluetooth connect/disconnect only for audio devices, via the audio driver's one-shot reconnect/disconnect request (`KSPROPSETID_BtAudio`), the same path Windows Settings uses; no handles to other processes |
| 2026-10-24 | Scanning is tied to what's on screen: Wi-Fi scans only while its list is open (≥ 8 s apart), Bluetooth discovery only while "Add a device" is open; the paired-device watcher is event-driven and always on |
| 2026-10-24 | Tiles side by side, macOS-style: the round icon toggles the radio, the rest of the tile picks the list below |
| 2026-10-24 | Mixer shows up to 4 apps (playing first, "System sounds" last); app names come from the session or the exe file name (Toolhelp snapshot, never a process handle) |
| 2026-10-24 | The test backdrop is click-through (layered + transparent), so a stray click during a hands-free test reaches your window |
| 2026-10-24 | **Auto-hide is the default** (your request): the island rests out of sight (new state *Tucked*) until the mouse touches the top edge in the middle, then behaves as before. Replaces the always-visible pill and the slim line; `layout.autoHide: false` restores them. No global mouse hook: the island's own invisible 2 px window at the edge receives the mouse |
| 2026-10-24 | Revealing pill slides down from above the top edge and fades in (90 ms); tucking slides up and fades out (160 ms) |
| 2026-10-24 | Sliders have no knob: a thick rounded bar (8 DIP, 10 DIP when hovered or held), the filled part is the level (your request) |
| 2026-10-24 | Lighter glass: tint 0.55 → 0.42 (your request: "a bit more translucent"); still tunable live with `glass.tintOpacity` |
| 2026-10-24 | Quick toggles (Microphone, Night light, Energy saver) sit in the header's empty middle, so the panel doesn't grow. A muted mic is red, the others use the accent |
| 2026-10-24 | Mic mute applies to every active microphone (so no app can listen through another one); unmute unmutes them all |
| 2026-10-24 | Night light and energy saver use Windows' own settings (no public switch exists); both check before writing and fall back to opening Settings |
| 2026-10-24 | Projection modes appear only with 2+ displays connected; switching runs off the UI thread; tests never switch it |
| 2026-10-24 | Closing with auto-hide: the panel shrinks and slides up into the edge at full opacity, fading only at the end (190 ms delay, 110 ms fade); the window drops to the strip 310 ms after |
| 2026-10-24 | The blur strength stays Windows' own (it can't be changed through the compositor); see "Later ideas" |

## 12. Update log

Newest first. Times are Nepal time (NPT, UTC+05:45). Each entry says what changed and what was verified.

| Date & time | Update |
|---|---|
| 2026-10-24 13:50 | **Phase 6 + quick toggles** (your request: night light and energy saver buttons, mic mute, Phase 6, a closing animation, less blur, animation ideas). New: `services/DisplayService.*`, `services/NightLightService.*`, widget `ToggleChip`; `AudioService` gained the microphone, `PowerService` energy saver (and the newer `GUID_ENERGY_SAVER_STATUS`, because the older status says "not available" while plugged in); the Displays column gained names with refresh rates and the Project list; the header gained the three toggles; columns 0.24/0.25/0.19/0.32. **Checked:** builds clean (Release + Debug), 211 unit checks, `--hardware` 222 (read-only for everything new), screenshots, closing timeline, 0.0 ms CPU tucked, 33.5 MB, no crashes. **Not checked by me:** switching night light, energy saver or the projection (each would have changed your screen or power settings): please click them once. **Blur:** not changed. The glass uses Windows' own blur, whose strength apps can't set (details in the chat). |
| 2026-10-24 13:19 | **Phase 5c** (your request: auto-hide like the taskbar, sliders without the knob that looked low, more translucent glass). `StateMachine` gained the *Tucked* state and `SetAutoHide`; `IslandWindow` shows it as an invisible strip at the top edge and slides the pill down/up (`IslandVisuals::SetPresent` fades it); `Slider` redrawn without a knob; settings `layout.autoHide` (true), `layout.revealWidth` (300), `glass.tintOpacity` 0.42; `TestHelpers.Touch()` + a touch test in `visual_check.ps1`. **Checked:** builds clean (Release + Debug), 178 checks, hands-free reveal/tuck timings, screenshots, 0.0 ms CPU tucked over 60 s, 32.8 MB, no crashes. **Found and fixed while testing:** after tucking, the empty window kept its old size for ~1.5 s (until the spring settled) and could have caught clicks on your browser tabs; it now drops to the strip as soon as the fade-out ends. |
| 2026-10-24 12:59 | **Visual check of Phase 5b** (`tests\visual_check.ps1`, hands-free): idle 250×51, Peek 426×78, control center 1476×486 px; every morph ~230 ms with no jumps, and a reversal halfway turns around smoothly. Fixed the test's width probe, which stopped at 1400 px (it now scans half the screen), so the wide panel's full width and its small overshoot are measured. No Island/DWM crashes in the event log. |
| 2026-10-24 12:55 | **Phase 5b: wide control center** (your request: wide and horizontal, Wi-Fi/Bluetooth lists, date/time/battery, per-app mixer). New: `views/sections/*` (the control center split into a header and four columns), `services/WifiService.*`, `services/BluetoothService.*`, `services/PowerService.*`; `AudioService` gained the mixer; widgets gained `ListRow`, compact side-by-side `ToggleTile`s and two-line labels; settings `panelWidth` 1180, new `panelMargin` 80, `panelRadius` 30. **Checked:** Release and Debug build with no warnings; 162 unit checks; `--hardware` 171 checks (mixer 1 % and back on Brave, Wi-Fi scan 9 networks, 7 Bluetooth devices listed; nothing switched or connected); hands-free screenshots of the panel and both lists; 32.9 MB RAM, handles steady, idle CPU ≤ 0.08 % of one core (Windows thread-pool ticks; none of Island's threads); no Island/DWM crashes in the event log. **Found and fixed while testing:** Windows takes 30 s to finish *any* Bluetooth device query on your PC (devices arrive in < 1 s, so nothing waits for the end anymore); a Wi-Fi scan asked for before the adapter was ready was dropped; the Connections column made the panel ~60 px taller than needed (tiles now side by side); long video titles were cut after one line (now two); only 4 of your 7 Bluetooth devices were reachable (lists page now, and headphones/speakers sort first); the selected tile was hard to see. Also: the test backdrop now lets clicks through. Note: your taskbar auto-hides, and Brave going fullscreen correctly hid the island several times during testing. |
| 2026-10-24 08:35 | **Phase 5 built.** New: `services/AudioService.*`, `services/BrightnessService.*`, `services/RadioService.*`, `services/Throttle.h`; the control center gained volume, brightness per display and the Wi-Fi/Bluetooth tiles; right-click opens Settings pages; `IslandTests --hardware`. **Checked:** Release and Debug build with no warnings; 135 checks pass (incl. 1 %-and-back hardware checks on both displays and the volume); the hands-free visual and media tests pass; 33.7 MB RAM, 0.0 ms idle CPU over 30 s. **Two crashes found in Windows' event log and fixed.** (1) `Island.exe` access violation on 08:04 (during a Phase 4 media test): I rebuilt that exact version, mapped the crash dump's address with a linker map → `MediaService::ReadPlayback` calling into an *empty* object Windows returned while the test player's session started. Every media/network object is now null-checked (E12). (2) **`dwm.exe` crashed 5 times (08:15–08:20)** when the Phase 5 build started: with Microsoft's public symbols the crash is in DWM's spring code (`CNaturalAnimation::SampleInitialValueAndFinalValueIfNecessary`). Cause: the control center was rebuilt right after its hidden sliders/toggles had started springs. Fix: hidden content never animates; replaced layers are released 3 s later (D11). After the fix: 5 launches + all test runs, 0 crashes. Also: Wi-Fi said "Not connected" when Windows hides the name (now "Connected"); a browser replacing its media session no longer makes the island jump to another app. |
| 2026-10-24 08:06 | **Phase 4 built.** New: `services/MediaService.*`, `services/MediaState.*` (session picking, app names, position, artwork decoding), `views/ControlCenter.*`, `views/Actions.h`, widgets `Artwork`, `ProgressBar`, `TextButton`; idle and Peek views show media; a click now opens the control center (the demo moved to the tray only); `tests/media_test.ps1`, `tests/TestHelpers.cs` (backdrop shrunk to 1000×620). **Checked:** builds with no warnings; 119 unit checks pass; your Spotify session renders with artwork; the silent test player is picked up within ~0.1 s of playing and released correctly; 31 MB RAM, 0.0 ms idle CPU. Found and fixed: `small` is a Windows macro; paused apps flip-flopped as Windows reshuffled its "current" session (now sticky); browser metadata updates stole the island from the app you'd just started (only play-starts count now); a missed event could leave another app's "playing" state stale (re-read on every sync, plus one follow-up look after each change notice). Note: a test run held the island in Peek over your browser's toolbar and caught one of your clicks there. Tests with poses now run as rarely as possible. |
| 2026-10-24 07:47 | **Phase 3 built.** New: `StateMachine.*`, `render/Graphics.*`, `render/Effects.*`, `render/Motion.*`, `render/Widgets.*`, `render/Glyphs.h`, `views/*` (idle, Peek placeholder, control kit demo), `tests/visual_check.ps1`; `IslandWindow` and `IslandVisuals` rewritten; settings gained `layout`, `glass` and `motion`; tray gained "Control kit demo" and "Keep open (for tuning)"; `--pose`. **Checked:** Release and Debug build with no warnings; 93 unit checks pass; `fullscreen_test.ps1` 8/8 again (slim line 5 px tall); hands-free visual check: screenshots of idle, Peek, control center, demo; morphs ~200–225 ms, 2–4 px overshoot, a reversal never jumps; **25 MB RAM, 0.0 ms idle CPU**. Found and fixed: the compositor refuses the colour-matrix effect unless all its properties are given; `RoundRect`/`DrawText` names clash with old Windows functions; a queued hover timer could open Peek after a window drag; your grey accent made "on" invisible. ⚠ A mouse-driven test run once found its test window behind your Chrome and clicked ~10 times near the middle of the primary screen before I added the "only click our own windows" guard. Those clicks may have landed in your Chrome window, so please check it. Tests that click now refuse to click anything but their own windows, and the visual test no longer clicks at all. **Not yet checked:** the feel by hand, and "transparency effects off" (it would change your desktop). |
| 2026-10-24 07:03 | **Phase 2 built.** New: `services/FullscreenWatcher.*` (foreground + under-the-island window, scoped move/resize hooks, taskbar `ABN_FULLSCREENAPP`, `SHQueryUserNotificationState`, blocklist), `services/WindowClassifier.*`, `platform/Settings.*` (settings.json, live reload), `platform/Log.*`, `App.*`, `config/settings.json`, `tests/UnitTests.cpp`, `tests/fullscreen_test.ps1`. `IslandWindow` now hides instantly and shows after the delay; the tray menu opens settings and the log. **Checked:** Release and Debug build with no warnings; 32 unit checks pass; all 8 checks of `fullscreen_test.ps1` pass (hide 0–12 ms, show after 507 ms); blocklist hides at once; a broken settings edit keeps the old settings; **idle CPU 0.0 ms over 20 s, 2.0 MB RAM, handle count steady**. Found and fixed: the log file was locked while Island ran. Experiment: cursor-move events are reported by a system thread, not the thread that moved the pointer, so the thread-scoped hooks stay quiet during games. **Not yet checked:** real games and PresentMon (you). |
| 2026-10-23 20:29 | **Phase 1 built.** New files: `IslandWindow.*` (window flags, top-center placement on the primary monitor, DPI, display-change handling, no-activate), `render/IslandVisuals.*` (compositor glass pill: blur-behind brush + tint + pill clip), `platform/Tray.*` (tray icon, Quit menu, Explorer-restart handling), `AppMessages.h`, `app.manifest` (Per-Monitor V2). `main.cpp` now handles single instance, replace-on-launch and `--quit`. The Phase 0 demo window was removed; it's in commit `f37f252`. **Checked (Release):** builds with no warnings; 250×51 px at the exact top-center of the Legion (125 %); always-on-top, no taskbar entry and never-takes-focus flags all set; launching doesn't steal focus; **1.9 MB RAM**; blur verified with a screenshot over a striped test pattern; a second launch replaces the first; `--quit` exits cleanly with code 0. **Not yet checked:** Debug build linking (blocked while your Phase 0 debug session was running), and the unplug/Alt+Tab/click checks for you. |
| 2026-10-23 20:12 | **Phase 0 done** (except your debugger walkthrough). **Project setup:** Git repo (branch `main`), `.gitignore`, `CMakeLists.txt`, `CMakePresets.json` (MSVC only), `.vscode/` (F5 = build + debug) and `CLAUDE.md`. **First program** (`src/main.cpp`): a plain Win32 window that uses C++/WinRT to read the Windows version and open Settings → Display on click. **Checked:** builds with MSVC 19.50 and no warnings. The Release exe is 370 KB, runs at 13 MB RAM (Task Manager "Memory") and closes cleanly with exit code 0. |
| 2026-10-23 ~20:05 | **Plan:** added the status section (§0), this update log and the build decisions. |
| 2026-10-23, before 20:08 | **Requirement:** every hover, click and toggle animates smoothly. Added per-control specs (§5), the control-kit step in Phase 3, and risks D7–D10. |
| 2026-10-23, before 20:08 | **Decisions:** one glass look on battery and plugged in; hide like the taskbar (anything fullscreen on the primary screen); no per-game list; projector risk removed (laptop stays primary). |
| 2026-10-23, before 20:08 | **Decisions:** C++ (lowest RAM); frosted glass idle pill; slim line over maximized windows; island follows the primary monitor (home: Legion 27Q-10, college: laptop). |
| 2026-10-23, before 20:08 | **Plan created:** goal and budgets, machine check, stack comparison, phases 0–7, game-safety rules, risk list, test matrix. |
