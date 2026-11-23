#!/usr/bin/env python3
"""PTT pedal check (stdlib only). Diagnostic tool, not production code.

  sudo python3 -I tools/ptt_probe.py            live: LEFT/RIGHT DOWN/RELEASE, RT START/STOP
  sudo python3 -I tools/ptt_probe.py --guide    prompted hold/release test with a verdict
  python3 -I tools/ptt_probe.py --replay FILE   re-read an evtest capture (no root)
  python3 -I tools/ptt_probe.py --selftest      check the analysis (no hardware)
"""
import argparse, fcntl, os, re, select, struct, sys, time

BTN = {272: "LEFT", 273: "RIGHT", 274: "MIDDLE"}
RT_BUTTON = "RIGHT"          # decision D-01
PULSE_MS = 50                # a foot press cannot be this short
EV = struct.Struct("llHHi")
EVIOCGRAB, EVIOCSCLOCKID = 0x40044590, 0x400445A0


class Tracker:
    """Turns (t, code, value) into readable events. No I/O."""
    def __init__(self):
        self.down = {}
        self.events = []      # (t, button, 'DOWN'|'RELEASE', held_seconds or None)

    def feed(self, t, code, value):
        b = BTN.get(code, f"CODE{code}")
        if value == 1:
            self.down[b] = t
            e = (t, b, "DOWN", None)
        elif value == 0:
            t0 = self.down.pop(b, None)
            e = (t, b, "RELEASE", None if t0 is None else t - t0)
        else:
            return None       # autorepeat
        self.events.append(e)
        return e

    @staticmethod
    def line(e, t0):
        t, b, what, held = e
        s = f"{t - t0:9.3f}  {b:<6} {what:<8}"
        if b == RT_BUTTON:
            s += "  RT START" if what == "DOWN" else "  RT STOP "
        if held is not None:
            s += f"  held {held * 1000:8.1f} ms"
            if held * 1000 < PULSE_MS:
                s += "   << PULSE, not a real hold"
        return s


def classify(events, t_hold, t_release, t_end, want):
    """One guided trial. Returns a kind and a short note."""
    inw = lambda a, b: [e for e in events if a <= e[0] < b]
    hold, rel = inw(t_hold, t_release), inw(t_release, t_end)
    names = lambda ev: ",".join(sorted({e[1] for e in ev if e[2] == "DOWN"})) or "-"
    long_holds = [e for e in hold + rel if e[2] == "RELEASE" and e[1] == want
                  and e[3] is not None and e[3] >= 1.0]
    if not hold and not rel:
        return "NONE", "no event"
    if long_holds and any(e[2] == "RELEASE" and e[1] == want for e in rel):
        return "HOLD", f"held {long_holds[0][3]:.1f} s"
    if hold and rel:
        return "EDGE", f"press sends {names(hold)}, release sends {names(rel)}"
    if hold:
        return "PULSE", f"press sends {names(hold)}, release sends nothing"
    return "LATE", f"only at release: {names(rel)}"


def verdict(kinds):
    if kinds and all(k == "HOLD" for k in kinds):
        return "PASS: down and release follow your foot"
    if kinds and all(k == "EDGE" for k in kinds):
        return "EDGE PULSES: separate pulses at press and release, no hold (fragile)"
    if kinds and all(k == "PULSE" for k in kinds):
        return "FAIL: pulse at press only, release is never reported"
    if kinds and all(k == "NONE" for k in kinds):
        return "FAIL: nothing received"
    return "INCONSISTENT: " + " ".join(kinds)


def find_device(vid="04d8", pid="0055"):
    base = "/sys/class/input"
    for n in sorted(os.listdir(base)):
        if not n.startswith("event"):
            continue
        rd = lambda f: open(f"{base}/{n}/device/{f}").read().strip()
        try:
            if rd("id/vendor") == vid and rd("id/product") == pid:
                return f"/dev/input/{n}", rd("name")
        except OSError:
            continue
    return None, None


class Device:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
        fcntl.ioctl(self.fd, EVIOCSCLOCKID, struct.pack("i", time.CLOCK_MONOTONIC))
        fcntl.ioctl(self.fd, EVIOCGRAB, 1)

    def read(self, timeout):
        r, _, _ = select.select([self.fd], [], [], max(0, timeout))
        out = []
        if r:
            data = os.read(self.fd, EV.size * 64)
            for i in range(0, len(data) - EV.size + 1, EV.size):
                s, us, typ, code, val = EV.unpack_from(data, i)
                if typ == 1:
                    out.append((s + us / 1e6, code, val))
        return out


def run_live(dev, say):
    tr, t0 = Tracker(), time.monotonic()
    say("Press and release. Ctrl+C to finish.\n")
    try:
        while True:
            for ev in dev.read(1.0):
                e = tr.feed(*ev)
                if e:
                    say(Tracker.line(e, t0))
    except KeyboardInterrupt:
        pass
    summary(tr, say)


def summary(tr, say):
    say("\nSUMMARY")
    for b in sorted({e[1] for e in tr.events}):
        d = sum(1 for e in tr.events if e[1] == b and e[2] == "DOWN")
        r = [e for e in tr.events if e[1] == b and e[2] == "RELEASE"]
        held = [e[3] * 1000 for e in r if e[3] is not None]
        p = sum(1 for h in held if h < PULSE_MS)
        longest = f"{max(held):.1f} ms" if held else "-"
        say(f"  {b:<6} down {d}  release {len(r)}  pulses {p}  longest hold {longest}")
    if not tr.events:
        say("  no events")


def run_guide(dev, say, trials=3, secs=5):
    tr, t0 = Tracker(), time.monotonic()

    def phase(text, dur):
        end = time.monotonic() + dur
        say(f"\n>>> {text}")
        while (left := end - time.monotonic()) > 0:
            for ev in dev.read(min(left, 0.5)):
                e = tr.feed(*ev)
                if e:
                    say("    " + Tracker.line(e, t0))

    results = {}
    for want in ("RIGHT", "LEFT"):
        kinds = []
        say(f"\n===== {want} BUTTON =====")
        phase(f"Foot OFF. Get ready for {want}.", 4)
        for i in range(trials):
            th = time.monotonic()
            phase(f"[{i + 1}/{trials}] PRESS {want} AND HOLD", secs)
            tl = time.monotonic()
            phase(f"[{i + 1}/{trials}] RELEASE", 4)
            k, note = classify(tr.events, th, tl, time.monotonic(), want)
            kinds.append(k)
            say(f"    result: {k} ({note})")
        results[want] = kinds
    say("\n===== VERDICT =====")
    for want, kinds in results.items():
        say(f"  {want:<6} {verdict(kinds)}")


def run_replay(path, say):
    tr, t0 = Tracker(), None
    # tolerates od/evtest interleaving in a shared pipe
    txt = open(path, errors="replace").read()
    pat = re.compile(r"time (\d+\.\d+),[^E]{0,80}?type 1 \(EV_KEY\), code (\d+) \([A-Z_]+\), value (\d)")
    for t, c, v in pat.findall(txt):
        t0 = t0 if t0 is not None else float(t)
        e = tr.feed(float(t), int(c), int(v))
        if e:
            say(Tracker.line(e, t0))
    summary(tr, say)


def selftest():
    def ev(*xs):
        tr = Tracker()
        for x in xs:
            tr.feed(*x)
        return tr.events
    hold = ev((1.5, 273, 1), (6.4, 273, 0))
    assert classify(hold, 1, 6, 10, "RIGHT")[0] == "HOLD"
    pulse = ev((1.5, 273, 1), (1.501, 273, 0))
    assert classify(pulse, 1, 6, 10, "RIGHT")[0] == "PULSE"
    edge = ev((1.5, 273, 1), (1.501, 273, 0), (6.4, 272, 1), (6.401, 272, 0))
    k, note = classify(edge, 1, 6, 10, "RIGHT")
    assert k == "EDGE" and "RIGHT" in note and "LEFT" in note
    assert classify([], 1, 6, 10, "RIGHT")[0] == "NONE"
    wrong = ev((1.5, 272, 1), (6.4, 272, 0))
    assert classify(wrong, 1, 6, 10, "RIGHT")[0] != "HOLD"
    assert verdict(["HOLD"] * 3).startswith("PASS")
    assert verdict(["PULSE"] * 3).startswith("FAIL")
    assert verdict(["HOLD", "PULSE", "HOLD"]).startswith("INCONSISTENT")
    assert "PULSE" in Tracker.line(pulse[1], 0) and "RT STOP" in Tracker.line(pulse[1], 0)
    assert "PULSE" not in Tracker.line(hold[1], 0) and "RT START" in Tracker.line(hold[0], 0)
    assert "RT" not in Tracker.line(edge[2], 0)
    print("selftest OK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--guide", action="store_true")
    ap.add_argument("--replay")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--device")
    ap.add_argument("--log")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    logf = open(a.log, "w") if a.log else None

    def say(s):
        print(s, flush=True)
        if logf:
            logf.write(s + "\n"); logf.flush()

    if a.replay:
        return run_replay(a.replay, say)
    path, name = (a.device, "given") if a.device else find_device()
    if not path:
        sys.exit("Pedal 04d8:0055 not found. Is it plugged in?")
    say(f"Device {path} ({name})")
    try:
        dev = Device(path)
    except PermissionError:
        sys.exit("Permission denied: run with sudo.")
    except OSError as e:
        sys.exit(f"Cannot open/grab {path}: {e}. Is evtest still running?")
    (run_guide if a.guide else run_live)(dev, say)


if __name__ == "__main__":
    main()
