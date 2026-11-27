# G1-P: foot pedal test (D-309, 06 §2), 2026-11-27

Pedal: Microchip "Preon", USB 04d8:0055, `/dev/input/by-id/usb-Microchip_Technology_Inc._Preon-event-mouse` (event3 today; the number may change). Mouse-type, `BTN_RIGHT` (273): 1 on press, 0 only on release (your evtest). On the panel, with nothing else running. Write OK, or what you saw, in the last column.

## 0. Install (you run it; I never run sudo)

1. Look first, changes nothing: `cd /home/user/projects/atc-trainer/.worktrees/runtime && ./deploy/setup_panel.sh --dry-run`
2. Install: `sudo ./deploy/setup_panel.sh`. It installs the headset settings (AGC off, capture level, kept at login and replug), the pedal rule, and tests a simulated headset replug and pedal access. The H540 and the pedal must be plugged in. Report: `~/atc-setup-report.txt`, last line `RESULT: PASS`. Any failure undoes everything.
3. Check without sudo: `./deploy/setup_panel.sh --verify`

| # | Do | Expect | Result |
|---|---|---|---|
| I1 | `udevadm info -q property -n /dev/input/by-id/usb-Microchip_Technology_Inc._Preon-event-mouse \| grep -E 'TAGS\|LIBINPUT_IGNORE'` | `uaccess` in TAGS, `LIBINPUT_IGNORE_DEVICE=1` | |
| I2 | `getfacl /dev/input/atc-pedal` | your user with `rw` (from `uaccess`; the app opens it read-only, D-200) | |
| I3 | Pedal pressed with the pointer on the desktop | **No** right-click menu | |

## 1. In the app

Start: `./build/bin/atc_trainer --size 1280x800` (no `--root`).

| # | Do | Expect | Result |
|---|---|---|---|
| P1 | Dashboard | PTT tile **OK**, "foot pedal (Preon)", **not** STAND-IN | |
| P2 | PTT test (tile button), hold the pedal 2 s | "Held 2.xx s: OK" | |
| P3 | Speech Lab: hold 5 s while speaking, release | One transmission of about 5 s, closed at the release (answers the 5 s hold of your evtest) | |
| P4 | Short tap (under a quarter second) | "Short tap cancelled", nothing recorded | |
| P5 | Hold more than 30 s | Closed at 30 s as STUCK; "Lift your foot to transmit again"; a new press works only after lifting | |
| P6 | Hold, unplug the pedal while held | Transmission closes (LOST); tile MISSING; "PTT disconnected" | |
| P7 | Plug it back in | Tile OK again within a few seconds, **without** `setfacl`; a transmission works | |
| P8 | Exercise: press while the pilot speaks | Pilot silent before TX opens (PILOT CUT); your new instruction is answered first (D-325) | |
| P9 | Exercise: unplug during the exercise, replug | Paused with the dialog; continues after the replug | |
| P10 | Log out of the desktop and in again; start the app | P1 again, without `setfacl` | |
| P11 | Reboot; start the app | P1 again, without `setfacl` | |
| P12 | Lab event list during P2 to P4 | Only RIGHT events; LEFT none (this pedal has no left) | |

## 2. Fallback (only if needed)

`./build/bin/atc_trainer --size 1280x800 --ptt-device stand-in`: the desk mouse, tile STAND-IN. It has no rule any more, so after each replug: `sudo setfacl -m u:user:r /dev/input/by-id/usb-LITEON_Dell_Wireless_Device-if01-event-mouse`.

## 3. Result

All rows OK: G1-P passed, D-02 passed for this pedal, D-03 closed. Any row not OK: tell me the row and what you saw.
