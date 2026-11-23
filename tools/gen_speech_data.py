#!/usr/bin/env python3
"""Builds all speech data from one phraseology file. Standard library only.
  tools/gen_speech_data.py data/speech/placeholder_phraseology.json OUTDIR
Writes sentences.txt (language model text), words.txt, aliases.txt, prompt.txt, grammar.gbnf,
grammar_open.gbnf and coverage.txt. Nothing depends on any scenario: the only input is the file.
Digits are uniform: a pool value gets no higher weight than a wrong value of the same shape."""
import json, random, re, sys

D = "zero one two three four five six seven eight nine".split()
PH = dict(zip("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima mike november oscar papa quebec romeo sierra tango uniform victor whiskey x-ray yankee zulu".split()))
SIDE = {"": "", "L": " left", "R": " right", "C": " center"}
spell = lambda s: " ".join(D[int(c)] for c in s)

def spoken(slot, v):
    if slot == "callsign":
        n, d = v.split("-"); return n.lower() + " " + spell(d)
    if slot == "runway":
        return spell(v[:2]) + SIDE[v[2:]]
    if slot == "taxiway":
        return PH[v[0]] + " " + spell(v[1:])
    if slot == "frequency":
        a, b = v.split("."); return spell(a) + " decimal " + spell(b)
    raise SystemExit(f"unknown pool {slot}")

def main(src, out):
    P = json.load(open(src)); pools = P["pools"]; rnd = random.Random(20261123)
    names = sorted({c.split("-")[0].lower() for c in pools["callsign"]})
    ltr = sorted({PH[t[0]] for t in pools["taxiway"]})
    dg = lambda n: spell("".join(rnd.choice("0123456789") for _ in range(n)))
    def rand(slot, name=None):
        if slot == "callsign": return (name or rnd.choice(names)) + " " + dg(3)
        if slot == "runway":
            return spell("%02d" % rnd.randint(1, 36)) + rnd.choice(["", "", "", " left", " right", " center"])
        if slot == "taxiway": return rnd.choice(ltr) + " " + dg(1)
        if slot == "heading": return spell("%03d" % rnd.randint(1, 360))
        if slot == "level": return spell("%03d" % rnd.randint(10, 450))
        if slot == "altitude":
            return rnd.choice(D[1:]) + " thousand" + rnd.choice(["", " " + rnd.choice(D[1:]) + " hundred"])
        if slot == "frequency": return spell(str(rnd.randint(118, 136))) + " decimal " + dg(rnd.choice([1, 1, 2, 3]))
        raise SystemExit(f"unknown slot {slot}")
    templates = [t for i in P["intents"] for t in i["atc"]]
    slots = lambda t: re.findall(r"\{(\w+)\}", t)
    def fill(t, over=None):
        s = t
        for x in slots(t):
            s = s.replace("{%s}" % x, (over or {}).get(x) or rand(x), 1)
        return s
    L = []
    for t in templates:
        for _ in range(400): L.append(fill(t))
        for x in set(slots(t)) & set(pools):           # every pool value in every slot it can take
            for v in pools[x]: L.append(fill(t, {x: spoken(x, v)}))
        for _ in range(40): L.append(fill(t, {"callsign": "[unk] " + dg(3)}))   # unknown callsign name
    var = {**P["spokenVariants"]["words"]}
    pron = P["voskPronunciations"]["words"]
    allv = {k: var.get(k, []) + pron.get(k, []) for k in set(var) | set(pron)}
    def vary(s, p): return " ".join(rnd.choice(allv[w]) if w in allv and rnd.random() < p else w for w in s.split())
    L += [vary(s, 0.35) for s in L]
    off = ["[unk]" + " [unk]" * rnd.randint(0, 9) for _ in range(len(L) // 8)]  # off-script speech
    rnd.shuffle(L)
    words = sorted({w for s in L for w in s.split()})
    w = lambda n, s: open(f"{out}/{n}", "w").write(s)
    w("sentences.txt", "\n".join(L + off) + "\n")
    w("words.txt", "\n".join(words) + "\n")
    w("aliases.txt", "# heard = meant. Raw text is kept in every log.\n" +
      "".join(f"{v} {k}\n" for k in sorted(allv) for v in allv[k]))
    kw = sorted({x for t in templates for x in re.sub(r"\{\w+\}", "", t).split()})
    w("prompt.txt", "Air traffic control radio. Callsigns " + ", ".join(n.capitalize() for n in names) +
      ". Taxiways " + ", ".join(l.capitalize() for l in ltr) + ". Words: " +
      "; ".join(dict.fromkeys(re.sub(r"\s+", " ", re.sub(r"\{\w+\}", "", t)).strip() for t in templates)) +
      "; decimal; left; right; center; niner.\n")
    q = lambda xs: " | ".join('"%s"' % x for x in xs)
    dig = q(D + ["niner", "tree", "fife"])
    body = {
        "digit": dig, "name": q(names),
        "callsign": '(name | other) " " digit " " digit " " digit', "other": "[a-z]+",
        "runway": 'digit " " digit (" left" | " right" | " center")?',
        "taxiway": '(%s) " " digit (" " digit)?' % q(sorted(PH.values())),
        "heading": 'digit " " digit " " digit', "level": 'digit " " digit " " digit',
        "altitude": 'digit (" " digit)? " thousand" (" " digit " hundred")?',
        "frequency": 'digit " " digit " " digit " decimal " digit (" " digit)? (" " digit)?'}
    def rule(t):
        parts = re.split(r"(\{\w+\})", t)
        return " ".join(p[1:-1] if p.startswith("{") else '"%s"' % p for p in parts if p)
    instr = "instr ::= " + " | ".join(rule(t) for t in templates)
    common = "\n".join(f"{k} ::= {v}" for k, v in body.items()) + "\n" + instr + "\n"
    w("grammar.gbnf", "# generated: instructions only\nroot ::= \" \" instr\n" + common)
    w("grammar_open.gbnf", "# generated: an instruction, or any other words\nroot ::= \" \" (instr | free)\nfree ::= [a-z]+ (\" \" [a-z]+)*\n" + common)
    text = set(" ".join(L).split()); missing = []
    blob = "\n".join(L)
    for p, vs in pools.items():
        for v in vs:
            if spoken(p, v) not in blob: missing.append(f"{p} {v}")
    w("coverage.txt", f"sentences {len(L) + len(off)}\npool values {sum(map(len, pools.values()))}\nmissing {len(missing)}\n" + "\n".join(missing))
    if missing: raise SystemExit("pool values missing from the data: " + ", ".join(missing))
    print(f"{len(L) + len(off)} sentences, {len(words)} words, all {sum(map(len, pools.values()))} pool values covered")

if __name__ == "__main__":
    if len(sys.argv) != 3: raise SystemExit(__doc__)
    main(*sys.argv[1:])
