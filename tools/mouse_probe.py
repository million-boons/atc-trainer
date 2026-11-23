#!/usr/bin/env python3
"""Mouse button press/release check (stdlib only). Diagnostic tool.

  python3 -I tools/mouse_probe.py --list             show mouse-type devices (no root)
  sudo python3 -I tools/mouse_probe.py               watch all of them
  sudo python3 -I tools/mouse_probe.py --device /dev/input/eventN
  python3 -I tools/mouse_probe.py --selftest         check the logic (no hardware)

RIGHT press = RT START, RIGHT release = RT STOP (decision D-01).
Devices are not grabbed, so clicks still act on the desktop.
"""
import argparse, fcntl, os, select, struct, sys, time

BTN = {272: "LEFT", 273: "RIGHT", 274: "MIDDLE"}
RT_BUTTON = "RIGHT"
PULSE_MS = 50
EV = struct.Struct("llHHi")            # 64-bit Linux input_event, 24 bytes
EV_KEY, EVIOCSCLOCKID = 1, 0x400445A0


def mice():
    """(path, name) of devices that report BTN_LEFT, from sysfs."""
    out, base = [], "/sys/class/input"
    for n in sorted((x for x in os.listdir(base) if x.startswith("event")), key=lambda x: int(x[5:])):
        try:
            words = open(f"{base}/{n}/device/capabilities/key").read().split()
            name = open(f"{base}/{n}/device/name").read().strip()
        except OSError:
            continue
        bits = 0
        for w in words:
            bits = (bits << 64) | int(w, 16)
        if bits >> 272 & 1:
            out.append((f"/dev/input/{n}", name))
    return out


class Buttons:
    """Turns raw input_event bytes into lines. One per device."""
    def __init__(self):
        self.down, self.stats = {}, {}

    def feed(self, data):
        lines = []
        for i in range(0, len(data) - EV.size + 1, EV.size):
            s, us, typ, code, val = EV.unpack_from(data, i)
            if typ != EV_KEY or code not in BTN or val not in (0, 1):
                continue
            t, b = s + us / 1e6, BTN[code]
            st = self.stats.setdefault(b, {"press": 0, "release": 0, "pulse": 0, "longest": 0.0})
            if val == 1:
                self.down[b] = t
                st["press"] += 1
                lines.append((t, b, "PRESS", "RT START" if b == RT_BUTTON else ""))
            else:
                st["release"] += 1
                t0 = self.down.pop(b, None)
                note = "RT STOP " if b == RT_BUTTON else ""
                if t0 is not None:
                    ms = (t - t0) * 1000
                    st["longest"] = max(st["longest"], ms)
                    note += f" held {ms:.1f} ms"
                    if ms < PULSE_MS:
                        st["pulse"] += 1
                        note += "  << PULSE, not a real hold"
                lines.append((t, b, "RELEASE", note.strip()))
        return lines


def watch(devs, say):
    fds, t0 = {}, time.monotonic()
    for path, name in devs:
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
            fcntl.ioctl(fd, EVIOCSCLOCKID, struct.pack("i", time.CLOCK_MONOTONIC))
        except PermissionError:
            sys.exit(f"Permission denied on {path}: run with sudo.")
        except OSError as e:
            say(f"skipped {path}: {e}")
            continue
        fds[fd] = (name, Buttons())
        say(f"watching {path}  {name}")
    if not fds:
        sys.exit("No device to watch.")
    say("\nHold a button a few seconds, release. Ctrl+C to finish.\n")
    active = set(fds)
    try:
        while active:
            for fd in select.select(list(active), [], [], 1.0)[0]:
                name, bt = fds[fd]
                try:
                    data = os.read(fd, EV.size * 64)
                except OSError:
                    say(f"device lost: {name}")
                    active.discard(fd)
                    continue
                for t, b, what, note in bt.feed(data):
                    say(f"{t - t0:9.3f}  {b:<6} {what:<8} {note:<52} [{name}]")
    except KeyboardInterrupt:
        pass
    say("\nSUMMARY")
    for name, bt in fds.values():
        for b, s in sorted(bt.stats.items()):
            say(f"  {b:<6} press {s['press']}  release {s['release']}  pulses {s['pulse']}"
                f"  longest hold {s['longest']:.1f} ms  [{name}]")


def selftest():
    pk = lambda t, code, val, typ=1: EV.pack(int(t), round((t % 1) * 1e6), typ, code, val)
    b = Buttons()
    out = b.feed(pk(1.0, 273, 1) + pk(1.0, 0, 5, 2) + pk(6.0, 273, 0))
    assert [o[2] for o in out] == ["PRESS", "RELEASE"]
    assert out[0][3] == "RT START" and "RT STOP" in out[1][3] and "held 5000.0" in out[1][3]
    assert "PULSE" not in out[1][3]
    out = b.feed(pk(7.0, 272, 1) + pk(7.001, 272, 0))
    assert "PULSE" in out[1][3] and "RT" not in out[0][3] + out[1][3]
    assert b.feed(pk(8.0, 273, 0))[0][3] == "RT STOP"      # release without press
    assert b.feed(pk(9.0, 30, 1)) == []                    # keyboard key ignored
    assert b.stats["RIGHT"]["press"] == 1 and b.stats["LEFT"]["pulse"] == 1
    print("selftest OK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--device")
    ap.add_argument("--log")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.list:
        for p, n in mice():
            print(p, n)
        return
    logf = open(a.log, "w") if a.log else None

    def say(s):
        print(s, flush=True)
        if logf:
            logf.write(s + "\n"); logf.flush()

    watch([(a.device, "given")] if a.device else mice(), say)


if __name__ == "__main__":
    main()
