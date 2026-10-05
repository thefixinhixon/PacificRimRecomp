#!/usr/bin/env python3
"""fix-tailcalls.py - Pacific Rim post-codegen fix.

Codegen leaves REX_FATAL stubs for a handful of cross-references its
FunctionNode analysis could not link, even though every target IS a
fully generated function:

  * Tail calls:  `b 0xTARGET` at the end of a function emits
        REX_FATAL("Unresolved call from 0xSRC to 0xTARGET");
    The target has a DEFINE_REX_FUNC body, so the stub becomes a plain
    call (the `return;` codegen already emitted after the stub keeps
    the tail-call semantics).

  * Branches into an address that is both a function entry and a local
    label inside the SAME generated body emit
        if (cond) REX_FATAL("Unresolved branch from 0xSRC to 0xTARGET");
    (or the unguarded fallthrough shape). The label exists in the same
    file, so the stub becomes `goto loc_TARGET;`.

The script asserts every replacement is justified (target defined /
label present in-file) and fails loudly otherwise.
"""
import re
import sys
from pathlib import Path

GEN = Path(__file__).resolve().parent.parent / "generated" / "default"

CALL_RE = re.compile(
    r'REX_FATAL\("Unresolved call from 0x[0-9A-F]+ to 0x([0-9A-F]+)"\);')
BRANCH_GUARDED_RE = re.compile(
    r'if \(([^;{}]+)\) REX_FATAL\("Unresolved branch from 0x[0-9A-F]+ to '
    r'0x([0-9A-F]+)"\);')
BRANCH_PLAIN_RE = re.compile(
    r'REX_FATAL\("Unresolved branch from 0x[0-9A-F]+ to 0x([0-9A-F]+)"\);')

files = sorted(GEN.glob("*.cpp"))
# Which targets are defined functions (across all generated files)?
defined = set()
for f in files:
    text = f.read_text()
    defined.update(re.findall(r"DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)", text))

n_call = n_branch = 0
for f in files:
    text = f.read_text()
    orig = text

    def fix_call(m):
        global n_call
        target = m.group(1)
        assert target in defined, f"{f.name}: call target {target} not defined"
        n_call += 1
        return f"sub_{target}(ctx, base);"

    def fix_branch_guarded(m):
        global n_branch
        cond, target = m.group(1), m.group(2)
        assert f"loc_{target}:" in text, (
            f"{f.name}: branch label loc_{target} not in this file")
        n_branch += 1
        return f"if ({cond}) goto loc_{target};"

    def fix_branch_plain(m):
        global n_branch
        target = m.group(1)
        assert f"loc_{target}:" in text, (
            f"{f.name}: branch label loc_{target} not in this file")
        n_branch += 1
        return f"goto loc_{target};"

    text = CALL_RE.sub(fix_call, text)
    text = BRANCH_GUARDED_RE.sub(fix_branch_guarded, text)
    text = BRANCH_PLAIN_RE.sub(fix_branch_plain, text)
    if text != orig:
        f.write_text(text)
        print(f"{f.name}: patched")

remaining = sum(
    f.read_text().count('REX_FATAL("Unresolved') for f in files)
print(f"tail-calls fixed: {n_call}, branches fixed: {n_branch}, "
      f"unresolved stubs remaining: {remaining}")
sys.exit(1 if remaining else 0)
