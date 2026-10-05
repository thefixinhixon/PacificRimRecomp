#!/usr/bin/env python3
"""fix-includes.py - Pacific Rim post-codegen fix (runs after fix-tailcalls).

Each pacificrim_recomp.N.cpp includes only its own partition header
(pacificrim_funcs.N.h), which declares exactly the callees codegen
linked. The tail-call fixer's converted stubs can call functions in
OTHER partitions whose declarations are therefore invisible in the
calling TU (error: use of undeclared identifier 'sub_...').

This step computes, per TU, the referenced sub_ callees not declared
by its included headers and not defined in the TU itself, and inserts
the declaring partitions' #include lines after the TU's own funcs
include. Idempotent.
"""
import re
import sys
from pathlib import Path

GEN = Path(__file__).resolve().parent.parent / "generated" / "default"

DECL_RE = re.compile(r'DECLARE_REX_FUNC\(sub_([0-9A-F]+)\)')
DEF_RE = re.compile(r'DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)')
CALL_RE = re.compile(r'\bsub_([0-9A-F]+)\(')
INC_RE = re.compile(r'#include "pacificrim_funcs\.(\d+)\.h"')
UMBRELLA_RE = re.compile(r'#include "pacificrim_funcs\.h"')

# sub -> partition index that declares it
declaring = {}
header_decls = {}
for h in sorted(GEN.glob("pacificrim_funcs.*.h")):
    idx = h.name.split(".")[1]
    if not idx.isdigit():
        continue
    decls = set(DECL_RE.findall(h.read_text(errors="replace")))
    header_decls[int(idx)] = decls
    for s in decls:
        declaring.setdefault(s, int(idx))

changed = 0
for f in sorted(GEN.glob("pacificrim_recomp.*.cpp")):
    text = f.read_text(errors="replace")
    if UMBRELLA_RE.search(text):
        continue  # sees everything already
    included = {int(m) for m in INC_RE.findall(text)}
    declared = set()
    for i in included:
        declared |= header_decls.get(i, set())
    declared |= set(DEF_RE.findall(text))
    referenced = set(CALL_RE.findall(text))
    missing = referenced - declared
    need = sorted({declaring[s] for s in missing if s in declaring})
    undeclared = [s for s in missing if s not in declaring]
    if undeclared:
        print(f"{f.name}: WARNING callees declared nowhere: "
              + ", ".join(sorted(undeclared)))
    add = [i for i in need if i not in included]
    if not add:
        continue
    lines = text.split("\n")
    # insert after the last existing funcs include line
    pos = max(i for i, ln in enumerate(lines) if INC_RE.search(ln))
    ins = [f'#include "pacificrim_funcs.{i}.h"' for i in add]
    lines[pos + 1:pos + 1] = ins
    f.write_text("\n".join(lines))
    changed += 1
    print(f"{f.name}: +includes {add}")

print(f"fix-includes: {changed} file(s) updated")
