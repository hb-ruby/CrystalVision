# CrystalVision: notes for Claude

Build, packaging, and setup steps are in `README.md`. This file covers context the code doesn't show.

## What it is
An X11 desktop app that detects nudity on screen using the NudeNet v3 ONNX detector.
Stack: C++20, ONNX Runtime C++ API, XCB (MIT-SHM capture; Composite with *automatic* redirection so it doesn't fight the compositor).

## Decisions
- **Model: NudeNet v3.4 640m**
- **License: AGPL-3.0** (matches NudeNet).
- **Target: Linux Mint 22 X11** (Ubuntu 24.04 base). Non-Debian distros are out of scope.
- **Distribution: `.deb`** built by GitHub Actions on `ubuntu-24.04`, bundling upstream ONNX Runtime 1.31.0. Ubuntu 24.04 has no ORT package, and building on a newer distro would require a newer glibc than Mint has.
- Renamed from RubyVision. Any leftover "RubyVision" is a bug.

## Dev environment
- Linux Mint 22 VM with desktop.
- CPU-only inference (no usable GPU), so model cost matters.

## Gotchas
- **Capture feedback loop:** exclude the app's own overlay/blur windows from the screen capture it analyzes.
- Model weights (`models/*.onnx`) aren't committed. Run `scripts/fetch-models.sh` after cloning.
- Pushing changes to `.github/workflows/` needs a GitHub token with the `workflow` scope.

## Roadmap
Personal tracking. Tick items off as they land, and add new ones as they come up.

Done:
- [x] Screen capture via MIT-SHM, including the wallpaper under window layers (`--dump-frame`)
- [x] Single-image NudeNet inference (`--detect`, `--detect-screen`), checked against Python NudeNet
- [x] `.deb` packaging and CI build on `ubuntu-24.04`
- [x] Continuous monitoring loop (`--monitor`): 1 s gap after each tick finishes; scale 0.333, every 5th tick 0.5, every 10th 1.0; inference capped at 4 threads
- [x] Black-box overlay over `*_EXPOSED` detections: one shaped, click-through, override-redirect window, excluded from capture; sharper-scan finds held until a same-or-sharper tick clears them
- [x] Calm/Alert scan states (`Scheduler`), replacing the fixed 1 s loop: Calm 0.333 every 5 s, every 6th at 1.0; Alert (any `triggersAlert` find, score ≥ 0.3) alternates 1.0/0.5 with 0.5 s gaps; back to Calm 30 s after the last trigger once no blocks are held
- [x] Screen change detection (`ChangeGate`, Calm only): skips a scan if no 60 px cell's mean colour moved > 5 or brightness > 45 since the last scan that ran (1.0 scans: since the last 1.0 scan); forced scan at least every 120 s. Contrast was dropped and brightness needs a high threshold: scrolling text moves both as much as a thumbnail does; only colour separates them. Weak spots: small black-and-white images, and colourful text (syntax highlighting, coloured UI) scrolling triggers scans

- [x] Autostart on login: XDG entry `/etc/xdg/autostart/crystalvision.desktop` (from `packaging/`, a dpkg conffile) runs `--monitor` in each user's graphical session, with `X-GNOME-AutoRestart=true` (Cinnamon's own daemons use it) for restart-on-crash. Not a systemd user unit: Cinnamon X11 never activates `graphical-session.target` (checked on the dev VM), so the unit couldn't follow login/logout. `--monitor` logs every tick only in a terminal or with `--verbose`; otherwise just start, Calm/Alert changes and stop, so `~/.xsession-errors` doesn't fill up

To do:
- [ ] Settings: label selection, score thresholds (adjustable; fixed for now at 0.5 to block, `kBlockScore`, and 0.3 to enter Alert), Calm/Alert intervals and exit time, change thresholds
- [ ] First tagged release (`v0.1.0`) and install test on a clean Mint 22. Check there that autostart fires at login and that `X-GNOME-AutoRestart` really respawns a killed monitor (not yet verified)
- [ ] User controls: still need to figure out. No in-app off switch for blocking

## Not doing
- Tamper resistance (root guard service, uninstall friction): built once, then dropped to keep the app simple. The user is mainly the owner; disabling or `sudo apt remove` is a deliberate choice, not something to defend against. `X-GNOME-AutoRestart` already covers an accidental kill

## Working with the user
- Use plain English but keep technical terms and detail.
