#!/usr/bin/env python3
"""Audit generated recomp TUs for cross-function gotos: a `goto loc_T`
is only valid if loc_T is defined inside the same C++ function body.
Reports every offending goto with the label's owning function/file."""
import re
import sys
from pathlib import Path

GEN = Path(sys.argv[1])
DEF_RE = re.compile(r'^DEFINE_REX_FUNC\(sub_([0-9A-F]+)')
LABEL_RE = re.compile(r'^loc_([0-9A-F]+):')
GOTO_RE = re.compile(r'goto loc_([0-9A-F]+);')

# pass 1: label -> (file, owner function)
label_owner = {}
func_labels = {}
for f in sorted(GEN.glob("pacificrim_recomp.*.cpp")):
    cur = None
    for line in f.read_text(errors="replace").split("\n"):
        m = DEF_RE.match(line)
        if m:
            cur = m.group(1)
            func_labels.setdefault((f.name, cur), set())
            continue
        m = LABEL_RE.match(line)
        if m and cur:
            label_owner[m.group(1)] = (f.name, cur)
            func_labels[(f.name, cur)].add(m.group(1))

bad = []
for f in sorted(GEN.glob("pacificrim_recomp.*.cpp")):
    cur = None
    for ln, line in enumerate(f.read_text(errors="replace").split("\n"), 1):
        m = DEF_RE.match(line)
        if m:
            cur = m.group(1)
            continue
        for t in GOTO_RE.findall(line):
            if t not in func_labels.get((f.name, cur), set()):
                owner = label_owner.get(t, ("<nowhere>", "<none>"))
                bad.append((f.name, cur, t, owner, ln))

print(f"cross-function gotos: {len(bad)}")
targets = {}
for fname, cur, t, owner, ln in bad:
    targets.setdefault(t, []).append((fname, cur, ln))
    print(f"{fname}:{ln} in sub_{cur}: goto loc_{t} (owned by sub_{owner[1]} in {owner[0]})")
print(f"\ndistinct bad targets: {len(targets)}")
for t in sorted(targets):
    print(f"0x{t} = {{ name = \"sub_{t}\" }}")
