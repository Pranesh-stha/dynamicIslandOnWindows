# Island: a Dynamic Island for Windows

**Read `PLAN.md` first.**

- §0: current status.
- §11: decisions log.
- §12: update log, newest first.

## Rules for every session

- **After any meaningful change** (code, a decision, a plan edit), add an entry at the **top** of the update log in `PLAN.md` §12.
  - Include the date and time in Nepal time (NPT, UTC+05:45; get it with `Get-Date`).
  - Say what changed and what was verified.
  - Then refresh §0 "Current status".
- Record new decisions in §11 "Decisions log."
- **Hard limits** (see `PLAN.md` §1 and §8):
  - RAM ≤ 100 MB (target 20–40 MB).
  - 0 % CPU when idle.
  - Never steal focus.
  - Hide whenever anything is fullscreen on the primary monitor.
  - No injection, no global input hooks, no handles to game processes.
- **Build only with MSVC through the presets:** `cmake --preset default`, then `cmake --build --preset debug` or `release`. MinGW g++ is on PATH and must not be used.
- **Compositor rule (PLAN.md risk D11):** content that isn't visible never animates, and replaced visual layers are released late (`View::Retire`). Breaking this crashed DWM and blanked the user's screen.
- **Testing:** run `ctest`, then the hands-free scripts in `tests/`. The user may be working at the PC: never click anything but the test's own windows, and check Windows' Application log (event 1000) for Island or `dwm.exe` crashes after test runs.
- The user is new to Windows app development. Explain new concepts in plain words.
