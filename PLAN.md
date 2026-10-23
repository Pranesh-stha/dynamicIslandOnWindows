# Island: a Dynamic Island for Windows

Working name: **Island**. This is a living plan, and we update it whenever a decision changes.

## 0. Current status (read this first)

- **Current phase:** Phase 1 is built and verified. A few hands-on checks are left for you (see the Phase 1 list). **Next: Phase 2, "Stay out of the way."**
- ⚠ **Until Phase 2 is done, the pill will show over fullscreen games.** Quit it from the tray icon before gaming. It doesn't start at login yet.
- **Last updated:** 2026-10-23 20:29 NPT (UTC+05:45)
- **Resuming in a new chat:** say "read PLAN.md".
  - This section says where things stand.
  - §11 lists every decision.
  - §12 is the update log: what changed and when, newest first.
- **Build:** run `cmake --preset default` once, then `cmake --build --preset debug` (or `release`).
  - The exe ends up in `build\Debug\Island.exe` or `build\Release\Island.exe`.
  - In VS Code, F5 closes any running island, builds the project and starts the debugger.
  - `Island.exe --quit` closes a running island. Launching a new copy replaces the old one.
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
 Idle: small pill ── hover ~150 ms ──►  Peek: wider, song + play/pause
   ▲                                       │ click
   │ mouse leaves ~300 ms                  ▼
   └───────────────────────────────  Expanded: control center
```

**Where it lives (decided):** on whichever monitor Windows currently calls *primary*. It moves automatically when that changes:

- At home, docked: the Legion 27Q-10.
- At college, laptop only: the laptop's built-in panel.

There is no setting to change; it just follows the primary monitor. With a college projector, the laptop screen stays primary, so the island stays on the laptop.

**Idle look (decided):** a frosted glass pill.

**Over maximized windows (decided):** when the window in front is maximized on the island's monitor, the pill shrinks to a slim glass line (about 4 px) so it doesn't cover tabs or title bars. The line touches the very top edge of the screen. Because the cursor stops at that edge, the line is easy to hit even though it's thin. Hovering still opens it.

**Expanded panel (draft):**

- Now playing: artwork, title/artist, ⏮ ⏯ ⏭ and a progress bar.
- Sliders: volume, and brightness for each monitor.
- Toggles: Wi-Fi and Bluetooth. Right-click opens the matching Settings page.
- Displays: a list of connected monitors. Clicking one opens Settings → Display.

**Motion rules (the "Apple feel"):**

- Use springs, not fixed-duration easing. Width, height and corner radius morph together.
- Content crossfades: the old content fades out quickly, and the new content fades in partway through the morph with a slight scale-up.
- **Interruptible:** if you reverse mid-animation, it continues from the current position *and speed*. It never snaps.
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
| Slider (volume, brightness) | Thumb grows; track thickens | While dragging, it follows the cursor 1:1 with no delay, because animating here would feel laggy. Clicking a spot on the track glides there with a spring, and changes from keyboard keys glide too |

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
  CMakeLists.txt
  PLAN.md
  src/
    main.cpp                 entry point, single-instance check, message loop
    app.manifest             DPI awareness (Per-Monitor V2), Windows 10/11
    AppMessages.h            "Island.Quit" message shared between Island processes
    IslandWindow.*           the window: flags, placement, DPI, hit-testing
    StateMachine.*           Hidden / Idle / Peek / Expanded
    render/
      IslandVisuals.*        visual tree, glass material, spring animations
      Text.*                 DirectWrite text + Segoe Fluent Icons (built into Win 11)
      Widgets.*              button, slider, toggle, album artwork
    services/                ← anything slow runs on background threads
      FullscreenWatcher.*    game / fullscreen detection
      MediaService.*         now playing (GlobalSystemMediaTransportControls)
      AudioService.*         volume (Core Audio)
      BrightnessService.*    laptop (WMI) + external monitor (DDC/CI)
      RadioService.*         Wi-Fi / Bluetooth (Windows.Devices.Radios)
      DisplayService.*       connected monitors, open Settings pages
    platform/
      Tray.*  Startup.*  Settings.*  Log.*
  config/
    settings.json            spring values, game blocklist, monitor choice
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

### Phase 2: Stay out of the way (game safety)
This comes before any visual polish, so you can leave it running while gaming from early on.

- Event-driven fullscreen/game detection (see §8).
- Really hide the window, not just make it transparent. Pause all work while hidden.
- Ignore hover while a window is being dragged, because the Windows 11 Snap Layouts bar lives at top-center.
- Optional exe blocklist in `settings.json`, as an escape hatch for any game the detection misses.
- **Done when:** the test matrix in §10 passes.

### Phase 3: Motion & glass (the feel)
- Visual tree, rounded-rectangle clip geometry, and the spring morph Idle ↔ Peek ↔ Expanded.
- Hover intent delay, leave delay and hysteresis (the area you have to leave is larger than the area you enter).
- Slim-line mode over maximized windows, with a smooth morph between the line and the pill.
- **Control kit:** button, toggle, radio/segmented and slider, with every hover and press animation from §5. We build and polish these on a demo panel first, so Phases 4–6 only reuse finished controls.
- Glass material: backdrop blur, a saturation boost, a dark tint, fine noise and a rim highlight. **Same look on battery and plugged in.** A solid fallback is only for when Windows itself blocks blur (D1, D6).
- Text with DirectWrite, icons from Segoe Fluent Icons.
- Spring values hot-reload from `settings.json`.
- **Done when:**
  - Morphs and every control animation feel smooth on both 144 and 165 Hz.
  - Interrupting an animation never snaps.
  - Idle CPU is 0 % and RAM < 40 MB.

### Phase 4: Now playing
- Pick the right media session: one that is playing beats one that is merely recent.
- Show title, artist and artwork, with play/pause/next/previous.
- Decode artwork at a small size (about 2× its display size).
- The compositor animates the progress bar, so there are no per-second redraws.
- **Done when:**
  - It works with Spotify, YouTube in Chrome/Edge, and Media Player.
  - Switching apps updates it within 1 s.
  - Memory is flat after an hour of track changes.

### Phase 5: Quick controls
- **Volume** slider and mute (Core Audio). It follows device changes such as plugging in headphones or connecting a Bluetooth headset.
- **Brightness** for the laptop panel (WMI) and the Legion 27Q-10 (DDC/CI). These calls run off-thread, only the newest value is sent, and writes are rate-limited.
- **Wi-Fi and Bluetooth** toggles (Radios API). Right-click opens the Settings page.
- **Done when:**
  - The sliders feel instant even though the hardware is slow.
  - Changes made elsewhere (keyboard volume keys, Quick Settings) show up in the island.

### Phase 6: Displays
- List connected monitors by friendly name.
- Clicking one opens `ms-settings:display`.
- Optional: an Extend / Duplicate / Second-screen-only switch (like Win+P).
- **Done when:** the list updates live on plug/unplug.

### Phase 7: Daily-driver hardening
- Start at login (Run key) plus an install script.
- `settings.json` with safe defaults. A corrupt file falls back to defaults instead of crashing.
- Crash log plus automatic restart.
- Correct state after sleep/resume, lock/unlock and GPU resets.
- **48-hour soak test:** the memory graph stays flat and the handle count doesn't grow.
- Polish: Island's own tray icon (currently Windows' generic app icon) and a dark-mode tray menu.
- **Done when:** it runs for a week without you thinking about it.

### Later ideas (not committed)
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
| AppBar `ABN_FULLSCREENAPP` notification (the same signal the taskbar uses to hide itself) | most fullscreen apps and games |
| Foreground-window change events (`SetWinEventHook`, out-of-context, so no injection) → check whether that window covers its whole monitor | borderless-windowed games, F11 browser, fullscreen video |
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

### E. Features
| # | What could go wrong | Why | Prevention |
|---|---|---|---|
| E1 | Wrong song shown (Spotify and YouTube both open) | Several media sessions | Priority: playing > most recently changed; manual switch |
| E2 | Artwork missing, late, or from the previous song | Artwork arrives asynchronously | Match artwork to the track; placeholder while loading |
| E3 | Progress bar wrong for some apps | Some apps don't report a timeline | Hide the bar when there's no timeline |
| E4 | Media updates stop after a while | Windows media events are sometimes flaky | Resync on session change and whenever the panel opens |
| E5 | Random crashes | UI touched from a background thread | Every callback is marshaled to the UI thread |
| E6 | Volume slider still controls the old device after plugging in headphones | Default device changed | Listen for default-device changes |
| E7 | Brightness slider feels laggy | WMI takes 50–500 ms and DDC/CI 50–200 ms per call | Background thread, send only the latest value, rate-limit |
| E8 | External brightness doesn't work | DDC/CI is off in the monitor's menu, or unsupported | Detect it, hide the slider, show a hint |
| E9 | Constant writes wear the monitor's settings memory | DDC/CI writes are stored in the monitor | A few writes per second at most, plus the final value on release |
| E10 | Wi-Fi name shows blank | Windows 11 24H2+ needs Location permission to read Wi-Fi names | Show "Connected" when permission is denied |
| E11 | Bluetooth toggle missing or wrong | Adapter disabled, or airplane mode on | Show the real state, never a fake one |

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

**Game / fullscreen matrix** (run it at the end of Phase 2 and again before every "release"):

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

## 12. Update log

Newest first. Times are Nepal time (NPT, UTC+05:45). Each entry says what changed and what was verified.

| Date & time | Update |
|---|---|
| 2026-10-23 20:29 | **Phase 1 built.** New files: `IslandWindow.*` (window flags, top-center placement on the primary monitor, DPI, display-change handling, no-activate), `render/IslandVisuals.*` (compositor glass pill: blur-behind brush + tint + pill clip), `platform/Tray.*` (tray icon, Quit menu, Explorer-restart handling), `AppMessages.h`, `app.manifest` (Per-Monitor V2). `main.cpp` now handles single instance, replace-on-launch and `--quit`. The Phase 0 demo window was removed; it's in commit `f37f252`. **Checked (Release):** builds with no warnings; 250×51 px at the exact top-center of the Legion (125 %); always-on-top, no taskbar entry and never-takes-focus flags all set; launching doesn't steal focus; **1.9 MB RAM**; blur verified with a screenshot over a striped test pattern; a second launch replaces the first; `--quit` exits cleanly with code 0. **Not yet checked:** Debug build linking (blocked while your Phase 0 debug session was running), and the unplug/Alt+Tab/click checks for you. |
| 2026-10-23 20:12 | **Phase 0 done** (except your debugger walkthrough). **Project setup:** Git repo (branch `main`), `.gitignore`, `CMakeLists.txt`, `CMakePresets.json` (MSVC only), `.vscode/` (F5 = build + debug) and `CLAUDE.md`. **First program** (`src/main.cpp`): a plain Win32 window that uses C++/WinRT to read the Windows version and open Settings → Display on click. **Checked:** builds with MSVC 19.50 and no warnings. The Release exe is 370 KB, runs at 13 MB RAM (Task Manager "Memory") and closes cleanly with exit code 0. |
| 2026-10-23 ~20:05 | **Plan:** added the status section (§0), this update log and the build decisions. |
| 2026-10-23, before 20:08 | **Requirement:** every hover, click and toggle animates smoothly. Added per-control specs (§5), the control-kit step in Phase 3, and risks D7–D10. |
| 2026-10-23, before 20:08 | **Decisions:** one glass look on battery and plugged in; hide like the taskbar (anything fullscreen on the primary screen); no per-game list; projector risk removed (laptop stays primary). |
| 2026-10-23, before 20:08 | **Decisions:** C++ (lowest RAM); frosted glass idle pill; slim line over maximized windows; island follows the primary monitor (home: Legion 27Q-10, college: laptop). |
| 2026-10-23, before 20:08 | **Plan created:** goal and budgets, machine check, stack comparison, phases 0–7, game-safety rules, risk list, test matrix. |
