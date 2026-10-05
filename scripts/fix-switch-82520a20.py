#!/usr/bin/env python3
"""fix-switch-82520a20.py — hand-complete a byte-table dispatch that
rexglue codegen under-analyzed in Pacific Rim's sub_82520A20.

Guest original (XEX disassembly, verified 2026-10-05):
  r0  = load8(0x820939F0 + r11)          ; class byte from table
  ctr = 0x82520ABC + r0                  ; entry = base + byte
  bctr                                   ; fall-through stub array

The stub array at 0x82520ABC is 7 stubs of 16 bytes plus a final
4-instruction stub that falls into the merge at 0x82520B3C.  Every
4-byte-aligned offset 0x00..0xA0 is a legal entry point (Duff-style):
an entry part-way through a stub executes the remainder of that stub.
rexglue emitted only `switch (r11) { case 0: goto loc_82520ABC;
default: __builtin_trap(); }`, so the first nonzero class (0x0C at
boot) trapped with SIGILL and deadlocked boot.

This patch replaces the switch + the case-0 stub in the generated
C++ with a dispatch on ctx.r0 covering entries 0x00..0xA0, translated
instruction-for-instruction from the XEX.  Bytes outside that range
(never produced by the game's table) fall to the merge block.

Idempotent: exits quietly if already applied.  Aborts loudly if any
anchor does not match exactly once.
"""
import re
import sys

PATH = "/mnt/sdb1/Games/PacificRim/project/generated/default/pacificrim_recomp.52.cpp"

src = open(PATH).read()

if "loc_82520ABC_e00" in src:
    print("already patched, nothing to do")
    sys.exit(0)


def replace_once(text, old, new, what):
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"ABORT: anchor for {what} matched {n} times (need 1)")
    return text.replace(old, new)


# ---------------------------------------------------------------- switch
# Find the switch whose only case jumps to loc_82520ABC.
m = re.search(
    r"\tswitch \(ctx\.r11\.u32\) \{\n\tcase 0:\n\t\tgoto loc_82520ABC;\n"
    r"\tdefault:\n\t\t__builtin_trap\(\); // Switch case out of range\n\t\}\n",
    src,
)
if not m:
    raise SystemExit("ABORT: trap switch for loc_82520ABC not found")

CASES = []
# stub-region entries 0x00..0x7C -> local labels
for b in range(0x00, 0x80, 4):
    CASES.append((b, f"loc_82520ABC_e{b:02X}"))
# merge-region entries
CASES += [
    (0x80, "loc_82520B3C"),
    (0x84, "loc_82520ABC_e84"),
    (0x88, "loc_82520ABC_e88"),
    (0x8C, "loc_82520B48"),
    (0x90, "loc_82520ABC_e90"),
    (0x94, "loc_82520ABC_e94"),
    (0x98, "loc_82520ABC_e98"),
    (0x9C, "loc_82520ABC_e9C"),
    (0xA0, "loc_82520B5C"),
]
lines = ["\tswitch (ctx.r0.u32) {"]
for b, lab in CASES:
    lines.append(f"\tcase 0x{b:02X}:")
    lines.append(f"\t\tgoto {lab};")
lines.append("\tdefault:")
lines.append("\t\tgoto loc_82520B3C;")
lines.append("\t}")
new_switch = "\n".join(lines) + "\n"
src = src[: m.start()] + new_switch + src[m.end():]

# ---------------------------------------------------------------- stub body
# Extract the original case-0 block to verify our templates line by line.
m2 = re.search(
    r"loc_82520ABC:\n(.*?)\tgoto loc_82520B3C;\n", src, re.DOTALL
)
if not m2:
    raise SystemExit("ABORT: case-0 block not found")
case0 = m2.group(1)
ADDI = "\tctx.r5.s64 = ctx.r1.s64 + 80;"
FMR = "\tctx.fpscr.disableFlushMode();\n\tctx.f1.f64 = ctx.f31.f64;"
if ADDI not in case0 or FMR not in case0:
    raise SystemExit("ABORT: case-0 addi/fmr lines differ from templates:\n" + case0)
if "sub_82520640(ctx, base);" not in case0 or "ctx.lr = 0x82520AC8;" not in case0:
    raise SystemExit("ABORT: case-0 bl lines differ from templates:\n" + case0)


def stub(off, bl_target=None, lr=None):
    """One 16-byte stub starting at entry offset `off` (relative to
    0x82520ABC).  Returns C++ lines with entry labels e{off}..e{off+0xC}."""
    out = []
    out.append(f"loc_82520ABC_e{off:02X}:")
    out.append("\t// addi r5,r1,80")
    out.append(ADDI)
    out.append(f"loc_82520ABC_e{off + 4:02X}:")
    out.append("\t// fmr f1,f31")
    out.append(FMR)
    out.append(f"loc_82520ABC_e{off + 8:02X}:")
    out.append(f"\t// bl 0x{bl_target:x}")
    out.append(f"\tctx.lr = 0x{lr:08X};")
    out.append(f"\tsub_{bl_target:08X}(ctx, base);")
    out.append(f"loc_82520ABC_e{off + 0xC:02X}:")
    out.append("\t// b 0x82520b3c")
    out.append("\tgoto loc_82520B3C;")
    return out


body = ["loc_82520ABC:"]
body += stub(0x00, 0x82520640, 0x82520AC8)
body += stub(0x10, 0x8251FFA8, 0x82520AD8)
body += stub(0x20, 0x8251FFA8, 0x82520AE8)
body += stub(0x30, 0x8251FFA8, 0x82520AF8)
body += stub(0x40, 0x8251F528, 0x82520B08)
body += stub(0x50, 0x8251FFA8, 0x82520B18)
body += stub(0x60, 0x82520188, 0x82520B28)
# final stub at 0x70: stw / mr / fmr / bl, falls through to merge
body.append("loc_82520ABC_e70:")
body.append("\t// stw r4,0(r3)")
body.append("\tREX_STORE_U32(ctx.r3.u32 + 0, ctx.r4.u32);")
body.append("loc_82520ABC_e74:")
body.append("\t// mr r5,r25")
body.append("\tctx.r5.u64 = ctx.r25.u64;")
body.append("loc_82520ABC_e78:")
body.append("\t// fmr f1,f31")
body.append(FMR)
body.append("loc_82520ABC_e7C:")
body.append("\t// bl 0x825207d8")
body.append("\tctx.lr = 0x82520B3C;")
body.append("\tsub_825207D8(ctx, base);")
body.append("\t// (falls through to loc_82520B3C)")
new_body = "\n".join(body) + "\n"
src = src[: m2.start()] + new_body + src[m2.end():]

# ------------------------------------------------------- merge entry labels
src = replace_once(
    src,
    "\tctx.r11.u64 = ctx.r31.u64 - ctx.r3.u64;\n"
    "\tctx.cr0.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);\n"
    "\t// bge 0x82520b48\n"
    "\tif (!ctx.cr0.lt) goto loc_82520B48;",
    "\tctx.r11.u64 = ctx.r31.u64 - ctx.r3.u64;\n"
    "\tctx.cr0.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);\n"
    "\t// bge 0x82520b48\n"
    "loc_82520ABC_e84:\n"
    "\tif (!ctx.cr0.lt) goto loc_82520B48;",
    "merge e84",
)
src = replace_once(
    src,
    "\t// mr r31,r3\n\tctx.r31.u64 = ctx.r3.u64;\nloc_82520B48:",
    "\t// mr r31,r3\nloc_82520ABC_e88:\n\tctx.r31.u64 = ctx.r3.u64;\nloc_82520B48:",
    "merge e88",
)
src = replace_once(
    src,
    "loc_82520B48:\n"
    "\t// addi r27,r27,1\n"
    "\tctx.r27.s64 = ctx.r27.s64 + 1;\n"
    "\t// addi r30,r30,2\n"
    "\tctx.r30.s64 = ctx.r30.s64 + 2;\n"
    "\t// addi r29,r29,4\n"
    "\tctx.r29.s64 = ctx.r29.s64 + 4;\n"
    "\t// cmpw cr6,r27,r26\n"
    "\tctx.cr6.compare<int32_t>(ctx.r27.s32, ctx.r26.s32, ctx.xer);\n"
    "\t// bne cr6,0x82520a7c\n"
    "\tif (!ctx.cr6.eq) goto loc_82520A7C;",
    "loc_82520B48:\n"
    "\t// addi r27,r27,1\n"
    "\tctx.r27.s64 = ctx.r27.s64 + 1;\n"
    "\t// addi r30,r30,2\n"
    "loc_82520ABC_e90:\n"
    "\tctx.r30.s64 = ctx.r30.s64 + 2;\n"
    "\t// addi r29,r29,4\n"
    "loc_82520ABC_e94:\n"
    "\tctx.r29.s64 = ctx.r29.s64 + 4;\n"
    "\t// cmpw cr6,r27,r26\n"
    "loc_82520ABC_e98:\n"
    "\tctx.cr6.compare<int32_t>(ctx.r27.s32, ctx.r26.s32, ctx.xer);\n"
    "\t// bne cr6,0x82520a7c\n"
    "loc_82520ABC_e9C:\n"
    "\tif (!ctx.cr6.eq) goto loc_82520A7C;",
    "merge block e90/e94/e98/e9C",
)

# ------------------------------------------------------------- includes
# New call targets live in pacificrim_funcs.18.h / pacificrim_funcs.h.
for hdr in ("pacificrim_funcs.18.h", "pacificrim_funcs.h"):
    inc = f'#include "{hdr}"'
    if inc not in src:
        # insert after the last existing #include at the top of the file
        incs = list(re.finditer(r'#include "[^"]+"\n', src))
        if not incs:
            raise SystemExit("ABORT: no #include lines found")
        pos = incs[-1].end()
        src = src[:pos] + inc + "\n" + src[pos:]
        print(f"added include {hdr}")

open(PATH, "w").write(src)
print("patched sub_82520A20 dispatch: switch on r0, entries 0x00..0xA0")
