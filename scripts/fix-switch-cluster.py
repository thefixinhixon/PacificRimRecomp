#!/usr/bin/env python3
"""fix-switch-cluster.py — hand-complete the byte-table dispatches of
the Pacific Rim 0x8251E/0x82520 record-walker cluster.

Same defect as fix-switch-82520a20.py (which see for the full story):
the guest dispatches on a table class byte (r0) into a fall-through
stub array at the jump base; rexglue emitted only case 0 and trapped
everything else.  Each function below was disassembled from the XEX
(capstone, guest dump 2026-10-05) and is translated instruction-for-
instruction, including entry labels inside the merge/tail blocks.

Idempotent per function; aborts loudly if any anchor mismatches.
"""
import glob
import re
import sys

GEN = "/mnt/sdb1/Games/PacificRim/project/generated/default"


def R(op):
    """Render one guest instruction (tuple) to generated-C++ lines."""
    k = op[0]
    if k == "bl":
        _, addr, tgt = op
        return [f"\t// bl 0x{tgt:x}",
                f"\tctx.lr = 0x{addr + 4:08X};",
                f"\tsub_{tgt:08X}(ctx, base);"]
    if k == "b":
        _, addr, tgt, label = op
        return [f"\t// b 0x{tgt:x}", f"\tgoto {label};"]
    if k == "addi":
        _, rd, ra, imm = op
        return [f"\t// addi r{rd},r{ra},{imm}",
                f"\tctx.r{rd}.s64 = ctx.r{ra}.s64 + {imm};"]
    if k == "add":
        _, rd, ra, rb = op
        return [f"\t// add r{rd},r{ra},r{rb}",
                f"\tctx.r{rd}.u64 = ctx.r{ra}.u64 + ctx.r{rb}.u64;"]
    if k == "mr":
        _, rd, ra = op
        return [f"\t// mr r{rd},r{ra}",
                f"\tctx.r{rd}.u64 = ctx.r{ra}.u64;"]
    if k == "lwz":
        _, rd, off, ra = op
        return [f"\t// lwz r{rd},{off}(r{ra})",
                f"\tctx.r{rd}.u64 = REX_LOAD_U32(ctx.r{ra}.u32 + {off});"]
    if k == "lfs":
        _, fd, off, ra = op
        return [f"\t// lfs f{fd},{off}(r{ra})",
                "\tctx.fpscr.disableFlushMode();",
                f"\ttemp.u32 = REX_LOAD_U32(ctx.r{ra}.u32 + {off});",
                f"\tctx.f{fd}.f64 = double(temp.f32);"]
    if k == "stw":
        _, rs, off, ra = op
        return [f"\t// stw r{rs},{off}(r{ra})",
                f"\tREX_STORE_U32(ctx.r{ra}.u32 + {off}, ctx.r{rs}.u32);"]
    if k == "fmr":
        _, fd, fa = op
        return [f"\t// fmr f{fd},f{fa}",
                "\tctx.fpscr.disableFlushMode();",
                f"\tctx.f{fd}.f64 = ctx.f{fa}.f64;"]
    if k == "clrlwi":
        _, rd, ra, n = op
        mask = (1 << (32 - n)) - 1
        return [f"\t// clrlwi r{rd},r{ra},{n}",
                f"\tctx.r{rd}.u64 = ctx.r{ra}.u32 & 0x{mask:X};"]
    if k == "addic":
        _, rd, ra, imm = op
        assert imm == -1, "renderer only supports addic imm=-1"
        return [f"\t// addic r{rd},r{ra},{imm}",
                f"\tctx.xer.ca = ctx.r{ra}.u32 > 0;",
                f"\tctx.r{rd}.s64 = ctx.r{ra}.s64 + {imm};"]
    if k == "subfe":
        _, rd, ra, rb = op
        return [f"\t// subfe r{rd},r{ra},r{rb}",
                f"\ttemp.u8 = (~ctx.r{ra}.u32 + ctx.r{rb}.u32 < ~ctx.r{ra}.u32) | "
                f"(~ctx.r{ra}.u32 + ctx.r{rb}.u32 + ctx.xer.ca < ctx.xer.ca);",
                f"\tctx.r{rd}.u64 = ~ctx.r{ra}.u64 + ctx.r{rb}.u64 + ctx.xer.ca;",
                "\tctx.xer.ca = temp.u8;"]
    raise SystemExit(f"renderer: unknown op {op}")


def E(jb, off):
    return f"loc_{jb:08X}_e{off:02X}"


def L(va):
    return f"loc_{va:08X}"


# Each spec:
#   file, func VA, jump base VA, merge VA,
#   region: [(addr, op-tuple...)] in address order (op tuples as in R,
#             with 'b' carrying its resolved label, 'bl' carrying addr),
#   cases: {offset: label} for every 4-byte entry 0..end,
#   tail_old / tail_new: exact merge..function-end block with the extra
#             entry labels inserted (or None if no insertions needed —
#             every function here needs some).
SPECS = []

# ------------------------------------------------------------ E/F walkers
def _tail_e360like(merge, addrd, addr1, addr2, addr4, cmpreg, loophead,
                   exit_va, jb):
    """Build (tail_old, tail_new) for the E360/F0B8 tail shape:
    merge: add racc; addi r27,1; addi r31,2; addi r30,4; cmpw; bne loop;
    exit: mr r3,racc; addi r1,144; restgprlr_25; return."""
    e = lambda off: E(jb, off)
    old = (
        f"{L(merge)}:\n"
        f"\t// add r{addrd},r3,r{addrd}\n"
        f"\tctx.r{addrd}.u64 = ctx.r3.u64 + ctx.r{addrd}.u64;\n"
        "\t// addi r27,r27,1\n"
        "\tctx.r27.s64 = ctx.r27.s64 + 1;\n"
        "\t// addi r31,r31,2\n"
        "\tctx.r31.s64 = ctx.r31.s64 + 2;\n"
        "\t// addi r30,r30,4\n"
        "\tctx.r30.s64 = ctx.r30.s64 + 4;\n"
        "\t// cmpw cr6,r27,r26\n"
        "\tctx.cr6.compare<int32_t>(ctx.r27.s32, ctx.r26.s32, ctx.xer);\n"
        f"\t// bne cr6,0x{loophead:x}\n"
        f"\tif (!ctx.cr6.eq) goto {L(loophead)};\n"
        f"{L(exit_va)}:\n"
        f"\t// mr r3,r{addrd}\n"
        f"\tctx.r3.u64 = ctx.r{addrd}.u64;\n"
        "\t// addi r1,r1,144\n"
        "\tctx.r1.s64 = ctx.r1.s64 + 144;\n"
        "\t// b 0x823ab4bc\n"
        "\t__restgprlr_25(ctx, base);\n"
        "\treturn;\n")
    new = (
        f"{L(merge)}:\n"
        f"\t// add r{addrd},r3,r{addrd}\n"
        f"\tctx.r{addrd}.u64 = ctx.r3.u64 + ctx.r{addrd}.u64;\n"
        "\t// addi r27,r27,1\n"
        f"{e(0x18)}:\n"
        "\tctx.r27.s64 = ctx.r27.s64 + 1;\n"
        "\t// addi r31,r31,2\n"
        f"{e(0x1C)}:\n"
        "\tctx.r31.s64 = ctx.r31.s64 + 2;\n"
        "\t// addi r30,r30,4\n"
        f"{e(0x20)}:\n"
        "\tctx.r30.s64 = ctx.r30.s64 + 4;\n"
        "\t// cmpw cr6,r27,r26\n"
        f"{e(0x24)}:\n"
        "\tctx.cr6.compare<int32_t>(ctx.r27.s32, ctx.r26.s32, ctx.xer);\n"
        f"\t// bne cr6,0x{loophead:x}\n"
        f"{e(0x28)}:\n"
        f"\tif (!ctx.cr6.eq) goto {L(loophead)};\n"
        f"{L(exit_va)}:\n"
        f"\t// mr r3,r{addrd}\n"
        f"\tctx.r3.u64 = ctx.r{addrd}.u64;\n"
        "\t// addi r1,r1,144\n"
        f"{e(0x30)}:\n"
        "\tctx.r1.s64 = ctx.r1.s64 + 144;\n"
        "\t// b 0x823ab4bc\n"
        f"{e(0x34)}:\n"
        "\t__restgprlr_25(ctx, base);\n"
        "\treturn;\n")
    return old, new


def _tail_e698like(merge, loophead, exit_va, jb):
    """E698/F260 tail: merge: add r31; addi r27,1; addi r30,2;
    addi r29,4; cmpw; bne; exit: mr r3,r31; addi r1,144; rest; return."""
    e = lambda off: E(jb, off)
    old = (
        f"{L(merge)}:\n"
        "\t// add r31,r3,r31\n"
        "\tctx.r31.u64 = ctx.r3.u64 + ctx.r31.u64;\n"
        "\t// addi r27,r27,1\n"
        "\tctx.r27.s64 = ctx.r27.s64 + 1;\n"
        "\t// addi r30,r30,2\n"
        "\tctx.r30.s64 = ctx.r30.s64 + 2;\n"
        "\t// addi r29,r29,4\n"
        "\tctx.r29.s64 = ctx.r29.s64 + 4;\n"
        "\t// cmpw cr6,r27,r26\n"
        "\tctx.cr6.compare<int32_t>(ctx.r27.s32, ctx.r26.s32, ctx.xer);\n"
        f"\t// bne cr6,0x{loophead:x}\n"
        f"\tif (!ctx.cr6.eq) goto {L(loophead)};\n"
        f"{L(exit_va)}:\n"
        "\t// mr r3,r31\n"
        "\tctx.r3.u64 = ctx.r31.u64;\n"
        "\t// addi r1,r1,144\n"
        "\tctx.r1.s64 = ctx.r1.s64 + 144;\n"
        "\t// b 0x823ab4bc\n"
        "\t__restgprlr_25(ctx, base);\n"
        "\treturn;\n")
    new = (
        f"{L(merge)}:\n"
        "\t// add r31,r3,r31\n"
        "\tctx.r31.u64 = ctx.r3.u64 + ctx.r31.u64;\n"
        "\t// addi r27,r27,1\n"
        f"{e(0x1C)}:\n"
        "\tctx.r27.s64 = ctx.r27.s64 + 1;\n"
        "\t// addi r30,r30,2\n"
        f"{e(0x20)}:\n"
        "\tctx.r30.s64 = ctx.r30.s64 + 2;\n"
        "\t// addi r29,r29,4\n"
        f"{e(0x24)}:\n"
        "\tctx.r29.s64 = ctx.r29.s64 + 4;\n"
        "\t// cmpw cr6,r27,r26\n"
        f"{e(0x28)}:\n"
        "\tctx.cr6.compare<int32_t>(ctx.r27.s32, ctx.r26.s32, ctx.xer);\n"
        f"\t// bne cr6,0x{loophead:x}\n"
        f"{e(0x2C)}:\n"
        f"\tif (!ctx.cr6.eq) goto {L(loophead)};\n"
        f"{L(exit_va)}:\n"
        "\t// mr r3,r31\n"
        "\tctx.r3.u64 = ctx.r31.u64;\n"
        "\t// addi r1,r1,144\n"
        f"{e(0x34)}:\n"
        "\tctx.r1.s64 = ctx.r1.s64 + 144;\n"
        "\t// b 0x823ab4bc\n"
        f"{e(0x38)}:\n"
        "\t__restgprlr_25(ctx, base);\n"
        "\treturn;\n")
    return old, new


def _tail_e428like(merge, loophead, exit_va, jb):
    """E428/F180 tail: merge: addi r27,1; addi r31,2; addi r30,4;
    cmpw; bne; exit: addi r1,144; restgprlr_25; return."""
    e = lambda off: E(jb, off)
    old = (
        f"{L(merge)}:\n"
        "\t// addi r27,r27,1\n"
        "\tctx.r27.s64 = ctx.r27.s64 + 1;\n"
        "\t// addi r31,r31,2\n"
        "\tctx.r31.s64 = ctx.r31.s64 + 2;\n"
        "\t// addi r30,r30,4\n"
        "\tctx.r30.s64 = ctx.r30.s64 + 4;\n"
        "\t// cmpw cr6,r27,r26\n"
        "\tctx.cr6.compare<int32_t>(ctx.r27.s32, ctx.r26.s32, ctx.xer);\n"
        f"\t// bne cr6,0x{loophead:x}\n"
        f"\tif (!ctx.cr6.eq) goto {L(loophead)};\n"
        f"{L(exit_va)}:\n"
        "\t// addi r1,r1,144\n"
        "\tctx.r1.s64 = ctx.r1.s64 + 144;\n"
        "\t// b 0x823ab4bc\n"
        "\t__restgprlr_25(ctx, base);\n"
        "\treturn;\n")
    new = (
        f"{L(merge)}:\n"
        "\t// addi r27,r27,1\n"
        "\tctx.r27.s64 = ctx.r27.s64 + 1;\n"
        "\t// addi r31,r31,2\n"
        f"{e(0x34)}:\n"
        "\tctx.r31.s64 = ctx.r31.s64 + 2;\n"
        "\t// addi r30,r30,4\n"
        f"{e(0x38)}:\n"
        "\tctx.r30.s64 = ctx.r30.s64 + 4;\n"
        "\t// cmpw cr6,r27,r26\n"
        f"{e(0x3C)}:\n"
        "\tctx.cr6.compare<int32_t>(ctx.r27.s32, ctx.r26.s32, ctx.xer);\n"
        f"\t// bne cr6,0x{loophead:x}\n"
        f"{e(0x40)}:\n"
        f"\tif (!ctx.cr6.eq) goto {L(loophead)};\n"
        f"{L(exit_va)}:\n"
        "\t// addi r1,r1,144\n"
        "\tctx.r1.s64 = ctx.r1.s64 + 144;\n"
        "\t// b 0x823ab4bc\n"
        f"{e(0x48)}:\n"
        "\t__restgprlr_25(ctx, base);\n"
        "\treturn;\n")
    return old, new


# --- sub_8251E360 (recomp.36) ---
_jb, _mg = 0x8251E3F0, 0x8251E404
_old, _new = _tail_e360like(_mg, 28, 27, 31, 30, 26, 0x8251E3B0, 0x8251E41C, _jb)
SPECS.append(dict(
    file="pacificrim_recomp.36.cpp", func=0x8251E360, jb=_jb, merge=_mg,
    region=[
        (0x8251E3F0, ("bl", 0x8251E3F0, 0x8251E360)),
        (0x8251E3F4, ("b", 0x8251E3F4, _mg, L(_mg))),
        (0x8251E3F8, ("addi", 28, 28, 1)),
        (0x8251E3FC, ("b", 0x8251E3FC, 0x8251E408, E(_jb, 0x18))),
        (0x8251E400, ("bl", 0x8251E400, 0x8251E508)),
    ],
    cases={0x00: E(_jb, 0x00), 0x04: L(_mg), 0x08: E(_jb, 0x08),
           0x0C: E(_jb, 0x18), 0x10: E(_jb, 0x10), 0x14: L(_mg),
           0x18: E(_jb, 0x18), 0x1C: E(_jb, 0x1C), 0x20: E(_jb, 0x20),
           0x24: E(_jb, 0x24), 0x28: E(_jb, 0x28), 0x2C: L(0x8251E41C),
           0x30: E(_jb, 0x30), 0x34: E(_jb, 0x34)},
    tail_old=_old, tail_new=_new))

# --- sub_8251E428 (recomp.24) ---
_jb, _mg = 0x8251E4B8, 0x8251E4E8
_old, _new = _tail_e428like(_mg, 0x8251E478, 0x8251E4FC, _jb)
SPECS.append(dict(
    file="pacificrim_recomp.24.cpp", func=0x8251E428, jb=_jb, merge=_mg,
    region=[
        (0x8251E4B8, ("mr", 4, 28)),
        (0x8251E4BC, ("bl", 0x8251E4BC, 0x8251E428)),
        (0x8251E4C0, ("b", 0x8251E4C0, _mg, L(_mg))),
        (0x8251E4C4, ("lwz", 11, 8, 3)),
        (0x8251E4C8, ("lwz", 3, 0, 28)),
        (0x8251E4CC, ("mr", 6, 11)),
        (0x8251E4D0, ("lwz", 5, 56, 11)),
        (0x8251E4D4, ("lfs", 1, 76, 11)),
        (0x8251E4D8, ("bl", 0x8251E4D8, 0x8252BBC8)),
        (0x8251E4DC, ("b", 0x8251E4DC, _mg, L(_mg))),
        (0x8251E4E0, ("mr", 4, 28)),
        (0x8251E4E4, ("bl", 0x8251E4E4, 0x8251E5C8)),
    ],
    cases={0x00: E(_jb, 0x00), 0x04: E(_jb, 0x04), 0x08: L(_mg),
           0x0C: E(_jb, 0x0C), 0x10: E(_jb, 0x10), 0x14: E(_jb, 0x14),
           0x18: E(_jb, 0x18), 0x1C: E(_jb, 0x1C), 0x20: E(_jb, 0x20),
           0x24: L(_mg), 0x28: E(_jb, 0x28), 0x2C: E(_jb, 0x2C),
           0x30: L(_mg), 0x34: E(_jb, 0x34), 0x38: E(_jb, 0x38),
           0x3C: E(_jb, 0x3C), 0x40: E(_jb, 0x40), 0x44: L(0x8251E4FC),
           0x48: E(_jb, 0x48)},
    tail_old=_old, tail_new=_new))

# --- sub_8251E698 (recomp.72) ---
_jb, _mg = 0x8251E728, 0x8251E740
_old, _new = _tail_e698like(_mg, 0x8251E6E8, 0x8251E758, _jb)
SPECS.append(dict(
    file="pacificrim_recomp.72.cpp", func=0x8251E698, jb=_jb, merge=_mg,
    region=[
        (0x8251E728, ("bl", 0x8251E728, 0x8251E698)),
        (0x8251E72C, ("b", 0x8251E72C, _mg, L(_mg))),
        (0x8251E730, ("lwz", 11, 44, 3)),
        (0x8251E734, ("add", 31, 11, 31)),
        (0x8251E738, ("b", 0x8251E738, 0x8251E744, E(_jb, 0x1C))),
        (0x8251E73C, ("bl", 0x8251E73C, 0x8251E938)),
    ],
    cases={0x00: E(_jb, 0x00), 0x04: L(_mg), 0x08: E(_jb, 0x08),
           0x0C: E(_jb, 0x0C), 0x10: E(_jb, 0x1C), 0x14: E(_jb, 0x14),
           0x18: L(_mg), 0x1C: E(_jb, 0x1C), 0x20: E(_jb, 0x20),
           0x24: E(_jb, 0x24), 0x28: E(_jb, 0x28), 0x2C: E(_jb, 0x2C),
           0x30: L(0x8251E758), 0x34: E(_jb, 0x34), 0x38: E(_jb, 0x38)},
    tail_old=_old, tail_new=_new))

# --- sub_8251F0B8 (recomp.12) ---
_jb, _mg = 0x8251F148, 0x8251F15C
_old, _new = _tail_e360like(_mg, 28, 27, 31, 30, 26, 0x8251F108, 0x8251F174, _jb)
SPECS.append(dict(
    file="pacificrim_recomp.12.cpp", func=0x8251F0B8, jb=_jb, merge=_mg,
    region=[
        (0x8251F148, ("bl", 0x8251F148, 0x8251E360)),
        (0x8251F14C, ("b", 0x8251F14C, _mg, L(_mg))),
        (0x8251F150, ("addi", 28, 28, 1)),
        (0x8251F154, ("b", 0x8251F154, 0x8251F160, E(_jb, 0x18))),
        (0x8251F158, ("bl", 0x8251F158, 0x8251E508)),
    ],
    cases={0x00: E(_jb, 0x00), 0x04: L(_mg), 0x08: E(_jb, 0x08),
           0x0C: E(_jb, 0x18), 0x10: E(_jb, 0x10), 0x14: L(_mg),
           0x18: E(_jb, 0x18), 0x1C: E(_jb, 0x1C), 0x20: E(_jb, 0x20),
           0x24: E(_jb, 0x24), 0x28: E(_jb, 0x28), 0x2C: L(0x8251F174),
           0x30: E(_jb, 0x30), 0x34: E(_jb, 0x34)},
    tail_old=_old, tail_new=_new))

# --- sub_8251F180 (recomp.29) ---
_jb, _mg = 0x8251F210, 0x8251F240
_old, _new = _tail_e428like(_mg, 0x8251F1D0, 0x8251F254, _jb)
SPECS.append(dict(
    file="pacificrim_recomp.29.cpp", func=0x8251F180, jb=_jb, merge=_mg,
    region=[
        (0x8251F210, ("mr", 4, 28)),
        (0x8251F214, ("bl", 0x8251F214, 0x8251E428)),
        (0x8251F218, ("b", 0x8251F218, _mg, L(_mg))),
        (0x8251F21C, ("lwz", 11, 8, 3)),
        (0x8251F220, ("lwz", 3, 0, 28)),
        (0x8251F224, ("mr", 6, 11)),
        (0x8251F228, ("lwz", 5, 56, 11)),
        (0x8251F22C, ("lfs", 1, 76, 11)),
        (0x8251F230, ("bl", 0x8251F230, 0x8252BBC8)),
        (0x8251F234, ("b", 0x8251F234, _mg, L(_mg))),
        (0x8251F238, ("mr", 4, 28)),
        (0x8251F23C, ("bl", 0x8251F23C, 0x8251E5C8)),
    ],
    cases={0x00: E(_jb, 0x00), 0x04: E(_jb, 0x04), 0x08: L(_mg),
           0x0C: E(_jb, 0x0C), 0x10: E(_jb, 0x10), 0x14: E(_jb, 0x14),
           0x18: E(_jb, 0x18), 0x1C: E(_jb, 0x1C), 0x20: E(_jb, 0x20),
           0x24: L(_mg), 0x28: E(_jb, 0x28), 0x2C: E(_jb, 0x2C),
           0x30: L(_mg), 0x34: E(_jb, 0x34), 0x38: E(_jb, 0x38),
           0x3C: E(_jb, 0x3C), 0x40: E(_jb, 0x40), 0x44: L(0x8251F254),
           0x48: E(_jb, 0x48)},
    tail_old=_old, tail_new=_new))

# --- sub_8251F260 (recomp.6) ---
_jb, _mg = 0x8251F2F0, 0x8251F308
_old, _new = _tail_e698like(_mg, 0x8251F2B0, 0x8251F320, _jb)
SPECS.append(dict(
    file="pacificrim_recomp.6.cpp", func=0x8251F260, jb=_jb, merge=_mg,
    region=[
        (0x8251F2F0, ("bl", 0x8251F2F0, 0x8251E698)),
        (0x8251F2F4, ("b", 0x8251F2F4, _mg, L(_mg))),
        (0x8251F2F8, ("lwz", 11, 44, 3)),
        (0x8251F2FC, ("add", 31, 11, 31)),
        (0x8251F300, ("b", 0x8251F300, 0x8251F30C, E(_jb, 0x1C))),
        (0x8251F304, ("bl", 0x8251F304, 0x8251E938)),
    ],
    cases={0x00: E(_jb, 0x00), 0x04: L(_mg), 0x08: E(_jb, 0x08),
           0x0C: E(_jb, 0x0C), 0x10: E(_jb, 0x1C), 0x14: E(_jb, 0x14),
           0x18: L(_mg), 0x1C: E(_jb, 0x1C), 0x20: E(_jb, 0x20),
           0x24: E(_jb, 0x24), 0x28: E(_jb, 0x28), 0x2C: E(_jb, 0x2C),
           0x30: L(0x8251F320), 0x34: E(_jb, 0x34), 0x38: E(_jb, 0x38)},
    tail_old=_old, tail_new=_new))

# ------------------------------------------------------- sub_825202B8
_jb, _mg = 0x82520350, 0x825203D0
_region = []
for _i, _t in enumerate([0x82520640, 0x8251FFA8, 0x8251FFA8, 0x8251FFA8,
                         0x8251F528, 0x8251FFA8, 0x82520188]):
    _s = _jb + 0x10 * _i
    _region += [(_s, ("addi", 5, 1, 80)),
                (_s + 4, ("fmr", 1, 31)),
                (_s + 8, ("bl", _s + 8, _t)),
                (_s + 0xC, ("b", _s + 0xC, _mg, L(_mg)))]
_region += [(0x825203C0, ("stw", 4, 0, 3)),
            (0x825203C4, ("fmr", 1, 31)),
            (0x825203C8, ("lwz", 5, 80, 1)),
            (0x825203CC, ("bl", 0x825203CC, 0x825207D8))]
_e = lambda off: E(_jb, off)
_old = (
    "loc_825203D0:\n"
    "\t// subf. r11,r3,r31\n"
    "\tctx.r11.u64 = ctx.r31.u64 - ctx.r3.u64;\n"
    "\tctx.cr0.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);\n"
    "\t// bge 0x825203dc\n"
    "\tif (!ctx.cr0.lt) goto loc_825203DC;\n"
    "\t// mr r31,r3\n"
    "\tctx.r31.u64 = ctx.r3.u64;\n"
    "loc_825203DC:\n"
    "\t// addi r27,r27,1\n"
    "\tctx.r27.s64 = ctx.r27.s64 + 1;\n"
    "\t// addi r30,r30,2\n"
    "\tctx.r30.s64 = ctx.r30.s64 + 2;\n"
    "\t// addi r29,r29,4\n"
    "\tctx.r29.s64 = ctx.r29.s64 + 4;\n"
    "\t// cmpw cr6,r27,r26\n"
    "\tctx.cr6.compare<int32_t>(ctx.r27.s32, ctx.r26.s32, ctx.xer);\n"
    "\t// bne cr6,0x82520310\n"
    "\tif (!ctx.cr6.eq) goto loc_82520310;\n"
    "loc_825203F0:\n"
    "\t// mr r3,r31\n"
    "\tctx.r3.u64 = ctx.r31.u64;\n"
    "\t// addi r1,r1,160\n"
    "\tctx.r1.s64 = ctx.r1.s64 + 160;\n"
    "\t// lfd f31,-72(r1)\n"
    "\tctx.fpscr.disableFlushMode();\n"
    "\tctx.f31.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);\n"
    "\t// b 0x823ab4bc\n"
    "\t__restgprlr_25(ctx, base);\n"
    "\treturn;\n")
_new = (
    "loc_825203D0:\n"
    "\t// subf. r11,r3,r31\n"
    "\tctx.r11.u64 = ctx.r31.u64 - ctx.r3.u64;\n"
    "\tctx.cr0.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);\n"
    "\t// bge 0x825203dc\n"
    f"{_e(0x84)}:\n"
    "\tif (!ctx.cr0.lt) goto loc_825203DC;\n"
    "\t// mr r31,r3\n"
    f"{_e(0x88)}:\n"
    "\tctx.r31.u64 = ctx.r3.u64;\n"
    "loc_825203DC:\n"
    "\t// addi r27,r27,1\n"
    "\tctx.r27.s64 = ctx.r27.s64 + 1;\n"
    "\t// addi r30,r30,2\n"
    f"{_e(0x90)}:\n"
    "\tctx.r30.s64 = ctx.r30.s64 + 2;\n"
    "\t// addi r29,r29,4\n"
    f"{_e(0x94)}:\n"
    "\tctx.r29.s64 = ctx.r29.s64 + 4;\n"
    "\t// cmpw cr6,r27,r26\n"
    f"{_e(0x98)}:\n"
    "\tctx.cr6.compare<int32_t>(ctx.r27.s32, ctx.r26.s32, ctx.xer);\n"
    "\t// bne cr6,0x82520310\n"
    f"{_e(0x9C)}:\n"
    "\tif (!ctx.cr6.eq) goto loc_82520310;\n"
    "loc_825203F0:\n"
    "\t// mr r3,r31\n"
    "\tctx.r3.u64 = ctx.r31.u64;\n"
    "\t// addi r1,r1,160\n"
    f"{_e(0xA4)}:\n"
    "\tctx.r1.s64 = ctx.r1.s64 + 160;\n"
    "\t// lfd f31,-72(r1)\n"
    f"{_e(0xA8)}:\n"
    "\tctx.fpscr.disableFlushMode();\n"
    "\tctx.f31.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);\n"
    "\t// b 0x823ab4bc\n"
    f"{_e(0xAC)}:\n"
    "\t__restgprlr_25(ctx, base);\n"
    "\treturn;\n")
_cases = {off: _e(off) for off in range(0, 0x80, 4)}
_cases.update({0x80: L(_mg), 0x84: _e(0x84), 0x88: _e(0x88),
               0x8C: L(0x825203DC), 0x90: _e(0x90), 0x94: _e(0x94),
               0x98: _e(0x98), 0x9C: _e(0x9C), 0xA0: L(0x825203F0),
               0xA4: _e(0xA4), 0xA8: _e(0xA8), 0xAC: _e(0xAC)})
SPECS.append(dict(file="pacificrim_recomp.38.cpp", func=0x825202B8,
                  jb=_jb, merge=_mg, region=_region, cases=_cases,
                  tail_old=_old, tail_new=_new))

# ------------------------------------------- sub_82520400 + sub_82520B70
def _bool_region(jb, mg):
    e4 = E(jb, 0x04)
    return [
        (jb + 0x00, ("bl", jb + 0x00, 0x82520770)),
        (jb + 0x04, ("mr", 9, 3)),
        (jb + 0x08, ("b", jb + 0x08, mg, L(mg))),
        (jb + 0x0C, ("bl", jb + 0x0C, 0x8251FEF8)),
        (jb + 0x10, ("b", jb + 0x10, jb + 4, e4)),
        (jb + 0x14, ("bl", jb + 0x14, 0x8251FF50)),
        (jb + 0x18, ("b", jb + 0x18, jb + 4, e4)),
        (jb + 0x1C, ("bl", jb + 0x1C, 0x82520080)),
        (jb + 0x20, ("b", jb + 0x20, jb + 4, e4)),
        (jb + 0x24, ("bl", jb + 0x24, 0x825200D8)),
        (jb + 0x28, ("b", jb + 0x28, jb + 4, e4)),
        (jb + 0x2C, ("bl", jb + 0x2C, 0x82520130)),
        (jb + 0x30, ("b", jb + 0x30, jb + 4, e4)),
        (jb + 0x34, ("bl", jb + 0x34, 0x82520260)),
        (jb + 0x38, ("b", jb + 0x38, jb + 4, e4)),
        (jb + 0x3C, ("bl", jb + 0x3C, 0x82520910)),
        (jb + 0x40, ("clrlwi", 11, 3, 24)),
        (jb + 0x44, ("addic", 10, 11, -1)),
        (jb + 0x48, ("subfe", 9, 10, 11)),
    ]


def _bool_cases(jb, mg):
    c = {off: E(jb, off) for off in range(0, 0x4C, 4)}
    c.update({0x4C: L(mg), 0x50: E(jb, 0x50), 0x54: E(jb, 0x54),
              0x58: E(jb, 0x58), 0x5C: E(jb, 0x5C), 0x60: E(jb, 0x60),
              0x64: E(jb, 0x64), 0x68: L(jb + 0x68), 0x6C: L(jb + 0x6C),
              0x70: E(jb, 0x70), 0x74: L(jb + 0x74)})
    return c


def _bool_tail(jb, mg, loophead, fail_va, epi_va):
    e = lambda off: E(jb, off)
    old = (
        f"{L(mg)}:\n"
        "\t// clrlwi. r11,r9,24\n"
        "\tctx.r11.u64 = ctx.r9.u32 & 0xFF;\n"
        "\tctx.cr0.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);\n"
        f"\t// beq 0x{fail_va:x}\n"
        f"\tif (ctx.cr0.eq) goto {L(fail_va)};\n"
        "\t// addi r28,r28,1\n"
        "\tctx.r28.s64 = ctx.r28.s64 + 1;\n"
        "\t// addi r31,r31,2\n"
        "\tctx.r31.s64 = ctx.r31.s64 + 2;\n"
        "\t// addi r30,r30,4\n"
        "\tctx.r30.s64 = ctx.r30.s64 + 4;\n"
        "\t// cmpw cr6,r28,r27\n"
        "\tctx.cr6.compare<int32_t>(ctx.r28.s32, ctx.r27.s32, ctx.xer);\n"
        f"\t// bne cr6,0x{loophead:x}\n"
        f"\tif (!ctx.cr6.eq) goto {L(loophead)};\n"
        f"{L(jb + 0x68)}:\n"
        "\t// li r3,1\n"
        "\tctx.r3.s64 = 1;\n"
        f"{L(epi_va)}:\n"
        "\t// addi r1,r1,144\n"
        "\tctx.r1.s64 = ctx.r1.s64 + 144;\n"
        "\t// b 0x823ab4c0\n"
        "\t__restgprlr_26(ctx, base);\n"
        "\treturn;\n"
        f"{L(fail_va)}:\n"
        "\t// li r3,0\n"
        "\tctx.r3.s64 = 0;\n"
        f"\t// b 0x{epi_va:x}\n"
        f"\tgoto {L(epi_va)};\n")
    new = (
        f"{L(mg)}:\n"
        "\t// clrlwi. r11,r9,24\n"
        "\tctx.r11.u64 = ctx.r9.u32 & 0xFF;\n"
        "\tctx.cr0.compare<int32_t>(ctx.r11.s32, 0, ctx.xer);\n"
        f"\t// beq 0x{fail_va:x}\n"
        f"{e(0x50)}:\n"
        f"\tif (ctx.cr0.eq) goto {L(fail_va)};\n"
        "\t// addi r28,r28,1\n"
        f"{e(0x54)}:\n"
        "\tctx.r28.s64 = ctx.r28.s64 + 1;\n"
        "\t// addi r31,r31,2\n"
        f"{e(0x58)}:\n"
        "\tctx.r31.s64 = ctx.r31.s64 + 2;\n"
        "\t// addi r30,r30,4\n"
        f"{e(0x5C)}:\n"
        "\tctx.r30.s64 = ctx.r30.s64 + 4;\n"
        "\t// cmpw cr6,r28,r27\n"
        f"{e(0x60)}:\n"
        "\tctx.cr6.compare<int32_t>(ctx.r28.s32, ctx.r27.s32, ctx.xer);\n"
        f"\t// bne cr6,0x{loophead:x}\n"
        f"{e(0x64)}:\n"
        f"\tif (!ctx.cr6.eq) goto {L(loophead)};\n"
        f"{L(jb + 0x68)}:\n"
        "\t// li r3,1\n"
        "\tctx.r3.s64 = 1;\n"
        f"{L(epi_va)}:\n"
        "\t// addi r1,r1,144\n"
        "\tctx.r1.s64 = ctx.r1.s64 + 144;\n"
        "\t// b 0x823ab4c0\n"
        f"{e(0x70)}:\n"
        "\t__restgprlr_26(ctx, base);\n"
        "\treturn;\n"
        f"{L(fail_va)}:\n"
        "\t// li r3,0\n"
        "\tctx.r3.s64 = 0;\n"
        f"\t// b 0x{epi_va:x}\n"
        f"\tgoto {L(epi_va)};\n")
    return old, new


_jb, _mg = 0x82520490, 0x825204DC
_old, _new = _bool_tail(_jb, _mg, 0x82520450, 0x82520504, 0x825204FC)
SPECS.append(dict(file="pacificrim_recomp.74.cpp", func=0x82520400,
                  jb=_jb, merge=_mg, region=_bool_region(_jb, _mg),
                  cases=_bool_cases(_jb, _mg),
                  tail_old=_old, tail_new=_new))

_jb, _mg = 0x82520C00, 0x82520C4C
_old, _new = _bool_tail(_jb, _mg, 0x82520BC0, 0x82520C74, 0x82520C6C)
SPECS.append(dict(file="pacificrim_recomp.80.cpp", func=0x82520B70,
                  jb=_jb, merge=_mg, region=_bool_region(_jb, _mg),
                  cases=_bool_cases(_jb, _mg),
                  tail_old=_old, tail_new=_new))

# ------------------------------------------------------------ apply
hdr_of = {}
for _h in glob.glob(GEN + "/pacificrim_funcs*.h"):
    _txt = open(_h).read()
    for _m in re.finditer(r"sub_([0-9A-F]{8})", _txt):
        hdr_of.setdefault(int(_m.group(1), 16), _h.split("/")[-1])


def replace_once(text, old, new, what):
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"ABORT: anchor for {what} matched {n} times")
    return text.replace(old, new)


for spec in SPECS:
    path = f"{GEN}/{spec['file']}"
    src = open(path).read()
    jb, mg = spec["jb"], spec["merge"]
    fresh = E(jb, 0) not in src
    if fresh:
        # 1) switch
        pat = re.compile(
            r"\tswitch \(ctx\.r11\.u32\) \{\n\tcase 0:\n\t\tgoto "
            + L(jb) + r";\n\tdefault:\n\t\t__builtin_trap\(\);"
            r" // Switch case out of range\n\t\}\n")
        m = pat.search(src)
        if not m:
            raise SystemExit(f"ABORT: trap switch not found for sub_{spec['func']:08X}")
        lines = ["\tswitch (ctx.r0.u32) {"]
        for off in sorted(spec["cases"]):
            lines.append(f"\tcase 0x{off:02X}:")
            lines.append(f"\t\tgoto {spec['cases'][off]};")
        lines.append("\tdefault:")
        lines.append(f"\t\tgoto {L(mg)};")
        lines.append("\t}")
        src = src[:m.start()] + "\n".join(lines) + "\n" + src[m.end():]
        # 2) region (case-0 block)
        pat2 = re.compile(re.escape(L(jb) + ":\n") + r"(.*?)\tgoto "
                          + L(mg) + r";\n", re.DOTALL)
        m2 = pat2.search(src)
        if not m2:
            raise SystemExit(f"ABORT: case-0 block not found for sub_{spec['func']:08X}")
        body = [L(jb) + ":"]
        for addr, op in spec["region"]:
            body.append(E(jb, addr - jb) + ":")
            body += R(op)
        src = src[:m2.start()] + "\n".join(body) + "\n" + src[m2.end():]
        # 3) tail labels
        src = replace_once(src, spec["tail_old"], spec["tail_new"],
                           f"sub_{spec['func']:08X} tail")
    # 4) includes for new call targets (always; no-op when present)
    targets = {op[2] for _, op in spec["region"] if op[0] == "bl"}
    for t in sorted(targets):
        hdr = hdr_of.get(t)
        if hdr and f'#include "{hdr}"' not in src:
            incs = list(re.finditer(r'#include "[^"]+"\n', src))
            if not incs:
                raise SystemExit(f"ABORT: no includes in {spec['file']}")
            pos = incs[-1].end()
            src = src[:pos] + f'#include "{hdr}"\n' + src[pos:]
            print(f"  + include {hdr} (sub_{t:08X}) into {spec['file']}")
    # 5) `temp` local for lfs/subfe users (always; no-op when present)
    if any(op[0] in ("lfs", "subfe") for _, op in spec["region"]):
        i = src.find(f"DEFINE_REX_FUNC(sub_{spec['func']:08X})")
        if i == -1:
            raise SystemExit(f"ABORT: function not found in {spec['file']}")
        j = src.find("DEFINE_REX_FUNC", i + 10)
        span = src[i: j if j != -1 else len(src)]
        if "PPCRegister temp{}" not in span:
            k = src.find("\tuint32_t ea{};\n", i)
            if k == -1:
                raise SystemExit(f"ABORT: ea decl not found for sub_{spec['func']:08X}")
            k += len("\tuint32_t ea{};\n")
            src = src[:k] + "\tPPCRegister temp{};\n" + src[k:]
            print(f"  + PPCRegister temp in sub_{spec['func']:08X}")
    open(path, "w").write(src)
    print(("patched" if fresh else "repaired") +
          f" sub_{spec['func']:08X} in {spec['file']}")
print("cluster patch complete")
