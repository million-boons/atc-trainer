#!/usr/bin/env python3
"""Levels and release-tail report for ptt_record folders (stdlib only). Audio stays on this machine.

  python3 -I tools/capture_report.py DIR [DIR ...]     one block per folder, for comparison
  python3 -I tools/capture_report.py --selftest

Tail: record with  ptt_record --tail-ms 500 ; every recording is then re-cut at 200, 300, 400
and 500 ms, so all tails are judged on the same speech. Same rule as UtteranceChecks:
released early = the last 3 frames of 20 ms are all at or above -40 dBFS.
"""
import array, json, math, os, sys, wave

SPEECH, FRAME, EDGE = -40.0, 320, 3
TAILS = (200, 300, 400, 500)


def db(v):
    return -120.0 if v <= 0 else 20 * math.log10(v / 32768)


def rms(x):
    return db(math.sqrt(sum(v * v for v in x) / len(x)))


def early(a):
    f = [rms(a[i:i + FRAME]) for i in range(0, len(a) - FRAME + 1, FRAME)]
    return len(f) >= EDGE and all(v >= SPEECH for v in f[-EDGE:])


def quant(x):
    x = sorted(x)
    return " / ".join(f"{x[round(q * (len(x) - 1))]:6.1f}" for q in (0, .25, .5, .75, 1))


def report(d):
    peaks, loud, quiet, over, unusable, n = [], [], [], 0, 0, 0
    tails = {t: [0, 0] for t in TAILS}          # early, judged
    with open(os.path.join(d, "manifest.jsonl")) as m:
        rows = [json.loads(l) for l in m if l.strip()]
    for r in rows:
        with wave.open(os.path.join(d, r["file"])) as w:
            a = array.array("h", w.readframes(w.getnframes()))
        n += 1
        over += r["overload"]
        unusable += not r["usable"]
        if r["empty"] or not len(a):
            continue
        peaks.append(db(max(abs(v) for v in a)))
        f = sorted(rms(a[i:i + FRAME]) for i in range(0, len(a) - FRAME + 1, FRAME))
        loud.append(f[int(len(f) * .9)])
        quiet.append(f[int(len(f) * .1)])
        if r["end"] != "normal":
            continue
        for t in TAILS:
            cut = r["tailSamples"] - t * 16
            if cut >= 0:
                tails[t][0] += early(a[:len(a) - cut])
                tails[t][1] += 1
    print(f"\n{d}: {n} recordings, {unusable} not usable, {over} overloaded")
    if not peaks:
        return
    inband = sum(-20 <= p <= -6 for p in peaks)
    print("                  min /    q1 /   med /    q3 /    max  dBFS")
    print(f"  peak         {quant(peaks)}")
    print(f"  loud frames  {quant(loud)}")
    print(f"  quiet frames {quant(quiet)}")
    print(f"  peaks in -20..-6 dBFS: {inband}/{len(peaks)} ({100 * inband / len(peaks):.0f} %)")
    for t, (e, j) in tails.items():
        if j:
            print(f"  tail {t} ms: released early {e}/{j} ({100 * e / j:.0f} %)")


def selftest():
    loud, soft = array.array("h", [3000] * 1600), array.array("h", [10] * 1600)
    assert early(loud) and not early(soft) and not early(loud + soft)
    assert not early(array.array("h"))
    assert abs(db(32768)) < 1e-9 and db(0) == -120
    print("selftest OK")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest()
    elif len(sys.argv) > 1 and not sys.argv[1].startswith("-"):
        for d in sys.argv[1:]:
            report(d)
    else:
        print(__doc__)
