#!/usr/bin/env python3
"""fix-switch-cluster2.py — hand-complete the byte-table dispatches
of Pacific Rim's remaining walker clusters (0x8228/0x8229/0x822C/
0x822F), same rexglue under-analysis defect as fix-switch-82520a20.py
and fix-switch-cluster.py (which see for the full story).  Every
function below was disassembled from the XEX (capstone, guest dump
2026-10-05) and is translated instruction-for-instruction.

Tail entry labels are inserted by anchoring on the exact comment +
statement line pairs inside each function's own span.
Idempotent per function; aborts loudly on any anchor mismatch.
"""
import glob
import re
import sys

GEN = "/mnt/sdb1/Games/PacificRim/project/generated/default"


def R(op):
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
    if k == "li":
        _, rd, imm = op
        return [f"\t// li r{rd},{imm}", f"\tctx.r{rd}.s64 = {imm};"]
    if k == "lwz":
        _, rd, off, ra = op
        return [f"\t// lwz r{rd},{off}(r{ra})",
                f"\tctx.r{rd}.u64 = REX_LOAD_U32(ctx.r{ra}.u32 + {off});"]
    if k == "lbz":
        _, rd, off, ra = op
        return [f"\t// lbz r{rd},{off}(r{ra})",
                f"\tctx.r{rd}.u64 = REX_LOAD_U8(ctx.r{ra}.u32 + {off});"]
    if k == "stw":
        _, rs, off, ra = op
        return [f"\t// stw r{rs},{off}(r{ra})",
                f"\tREX_STORE_U32(ctx.r{ra}.u32 + {off}, ctx.r{rs}.u32);"]
    if k == "stwx":
        _, rs, ra, rb = op
        return [f"\t// stwx r{rs},r{ra},r{rb}",
                f"\tREX_STORE_U32(ctx.r{ra}.u32 + ctx.r{rb}.u32, ctx.r{rs}.u32);"]
    if k == "mtctr":
        _, ra = op
        return [f"\t// mtctr r{ra}", f"\tctx.ctr.u64 = ctx.r{ra}.u64;"]
    if k == "bctrl":
        _, addr = op
        return ["\t// bctrl ",
                f"\tctx.lr = 0x{addr + 4:08X};",
                "\tREX_CALL_INDIRECT_FUNC(ctx.ctr.u32);"]
    if k == "cmpwi6":
        _, ra, imm = op
        return [f"\t// cmpwi cr6,r{ra},{imm}",
                f"\tctx.cr6.compare<int32_t>(ctx.r{ra}.s32, {imm}, ctx.xer);"]
    if k == "cmplwi0":
        _, ra, imm = op
        return [f"\t// cmplwi r{ra},{imm}",
                f"\tctx.cr0.compare<uint32_t>(ctx.r{ra}.u32, {imm}, ctx.xer);"]
    if k == "beq6":
        _, tgt, label = op
        return [f"\t// beq cr6,0x{tgt:x}", f"\tif (ctx.cr6.eq) goto {label};"]
    if k == "bne0":
        _, tgt, label = op
        return [f"\t// bne 0x{tgt:x}", f"\tif (!ctx.cr0.eq) goto {label};"]
    if k == "slwi":
        _, rd, ra, sh = op
        mask = (0xFFFFFFFF << sh) & 0xFFFFFFFF
        return [f"\t// slwi r{rd},r{ra},{sh}",
                f"\tctx.r{rd}.u64 = __builtin_rotateleft64(ctx.r{ra}.u32 | "
                f"(ctx.r{ra}.u64 << 32), {sh}) & 0x{mask:X};"]
    raise SystemExit(f"renderer: unknown op {op}")


def E(jb, off):
    return f"loc_{jb:08X}_e{off:02X}"


def L(va):
    return f"loc_{va:08X}"


SPECS = []


def pair_chain(jb, mg, first, mid, last):
    """bl X; b merge stubs (8 bytes each), last falls into merge."""
    reg = []
    seq = [first] + [mid] * 6 + [last]
    for i, t in enumerate(seq):
        a = jb + 8 * i
        reg.append((a, ("bl", a, t)))
        if i < len(seq) - 1:
            reg.append((a + 4, ("b", a + 4, mg, L(mg))))
    cases = {}
    for off in range(0, 8 * len(seq), 4):
        cases[off] = E(jb, off) if off % 8 == 0 else L(mg)
    return reg, cases


def mr_chain(jb, mg, targets, argreg=28):
    """mr r4,r{argreg}; bl X; b merge stubs (12 bytes), last falls in."""
    reg = []
    for i, t in enumerate(targets):
        a = jb + 12 * i
        reg.append((a, ("mr", 4, argreg)))
        reg.append((a + 4, ("bl", a + 4, t)))
        if i < len(targets) - 1:
            reg.append((a + 8, ("b", a + 8, mg, L(mg))))
    cases = {off: E(jb, off) for off in range(0, 12 * len(targets), 4)}
    return reg, cases


def tail_ins(jb, items):
    """items: (offset, comment_line, stmt_line) -> insertion tuples."""
    return [(c, s, E(jb, o)) for (o, c, s) in items]


ADDI = lambda r, imm=1: (f"\t// addi r{r},r{r},{imm}",
                         f"\tctx.r{r}.s64 = ctx.r{r}.s64 + {imm};")
CMPW = lambda a, b: (f"\t// cmpw cr6,r{a},r{b}",
                     f"\tctx.cr6.compare<int32_t>(ctx.r{a}.s32, ctx.r{b}.s32, ctx.xer);")
BNE = lambda loop: (f"\t// bne cr6,0x{loop:x}",
                    f"\tif (!ctx.cr6.eq) goto {L(loop)};")
REST = lambda n, tgt="0x823ab4bc": (f"\t// b {tgt}",
                                    f"\t__restgprlr_{n}(ctx, base);")
ADDI1 = ("\t// addi r1,r1,144", "\tctx.r1.s64 = ctx.r1.s64 + 144;")

# --- S1 sub_82286E50 (recomp.69), pair-chain w/ add-accumulate tail ---
_jb, _mg = 0x82286EE0, 0x82286F1C
_reg, _cases = pair_chain(_jb, _mg, 0x82286E50, 0x82279270, 0x82287240)
_cases.update({0x3C: L(_mg), 0x40: E(_jb, 0x40), 0x44: E(_jb, 0x44),
               0x48: E(_jb, 0x48), 0x4C: E(_jb, 0x4C), 0x50: E(_jb, 0x50),
               0x54: L(0x82286F34), 0x58: E(_jb, 0x58), 0x5C: E(_jb, 0x5C)})
_ins = tail_ins(_jb, [
    (0x40, *ADDI(27)), (0x44, *ADDI(30, 2)), (0x48, *ADDI(29, 4)),
    (0x4C, *CMPW(27, 26)), (0x50, *BNE(0x82286EA0)),
    (0x58, *ADDI1), (0x5C, *REST(25))])
SPECS.append(dict(file="pacificrim_recomp.69.cpp", func=0x82286E50,
                  jb=_jb, merge=_mg, region=_reg, cases=_cases, ins=_ins))

# --- S2 sub_822C00D8 (recomp.49), mr-chain ---
_jb, _mg = 0x822C0168, 0x822C01C4
_reg, _cases = mr_chain(_jb, _mg, [0x8229D8A0, 0x822BDBB0, 0x822BDBB0,
                                   0x822BDBB0, 0x822880C8, 0x822BDBB0,
                                   0x822BDC88, 0x822BDD60])
_cases.update({0x5C: L(_mg), 0x60: E(_jb, 0x60), 0x64: E(_jb, 0x64),
               0x68: E(_jb, 0x68), 0x6C: E(_jb, 0x6C),
               0x70: L(0x822C01D8), 0x74: E(_jb, 0x74)})
_ins = tail_ins(_jb, [
    (0x60, *ADDI(31, 2)), (0x64, *ADDI(30, 4)), (0x68, *CMPW(27, 26)),
    (0x6C, *BNE(0x822C0128)), (0x74, *REST(25))])
SPECS.append(dict(file="pacificrim_recomp.49.cpp", func=0x822C00D8,
                  jb=_jb, merge=_mg, region=_reg, cases=_cases, ins=_ins))

# --- S3 sub_822C12E8 (recomp.32), mr-chain ---
_jb, _mg = 0x822C1378, 0x822C13D4
_reg, _cases = mr_chain(_jb, _mg, [0x8229D8A0, 0x822BDBB0, 0x822BDBB0,
                                   0x822BDBB0, 0x822880C8, 0x822BDBB0,
                                   0x822BDC88, 0x822BDD60])
_cases.update({0x5C: L(_mg), 0x60: E(_jb, 0x60), 0x64: E(_jb, 0x64),
               0x68: E(_jb, 0x68), 0x6C: E(_jb, 0x6C),
               0x70: L(0x822C13E8), 0x74: E(_jb, 0x74)})
_ins = tail_ins(_jb, [
    (0x60, *ADDI(31, 2)), (0x64, *ADDI(30, 4)), (0x68, *CMPW(27, 26)),
    (0x6C, *BNE(0x822C1338)), (0x74, *REST(25))])
SPECS.append(dict(file="pacificrim_recomp.32.cpp", func=0x822C12E8,
                  jb=_jb, merge=_mg, region=_reg, cases=_cases, ins=_ins))

# --- S4 sub_8228CD38 (recomp.27), pair-chain ---
_jb, _mg = 0x8228CDC8, 0x8228CE04
_reg, _cases = pair_chain(_jb, _mg, 0x82286E50, 0x82279270, 0x82287240)
_cases.update({0x3C: L(_mg), 0x40: E(_jb, 0x40), 0x44: E(_jb, 0x44),
               0x48: E(_jb, 0x48), 0x4C: E(_jb, 0x4C), 0x50: E(_jb, 0x50),
               0x54: L(0x8228CE1C), 0x58: E(_jb, 0x58), 0x5C: E(_jb, 0x5C)})
_ins = tail_ins(_jb, [
    (0x40, *ADDI(27)), (0x44, *ADDI(30, 2)), (0x48, *ADDI(29, 4)),
    (0x4C, *CMPW(27, 26)), (0x50, *BNE(0x8228CD88)),
    (0x58, *ADDI1), (0x5C, *REST(25))])
SPECS.append(dict(file="pacificrim_recomp.27.cpp", func=0x8228CD38,
                  jb=_jb, merge=_mg, region=_reg, cases=_cases, ins=_ins))

# --- S5 sub_8228D078 (recomp.59), pair-chain, counter tail ---
_jb, _mg = 0x8228D104, 0x8228D140
_reg, _cases = pair_chain(_jb, _mg, 0x8228D078, 0x82288188, 0x8228D538)
_cases.update({0x3C: L(_mg), 0x40: E(_jb, 0x40), 0x44: E(_jb, 0x44),
               0x48: E(_jb, 0x48), 0x4C: E(_jb, 0x4C),
               0x50: L(0x8228D154), 0x54: E(_jb, 0x54)})
_ins = tail_ins(_jb, [
    (0x40, *ADDI(31, 2)), (0x44, *ADDI(30, 4)), (0x48, *CMPW(28, 27)),
    (0x4C, *BNE(0x8228D0C4)), (0x54, *REST(26, "0x823ab4c0"))])
SPECS.append(dict(file="pacificrim_recomp.59.cpp", func=0x8228D078,
                  jb=_jb, merge=_mg, region=_reg, cases=_cases, ins=_ins))

# --- S6 sub_8228D6F0 (recomp.35), mixed worker stub ---
_jb, _mg = 0x8228D780, 0x8228D7B4
_reg = [
    (0x8228D780, ("bl", 0x8228D780, 0x8228D6F0)),
    (0x8228D784, ("b", 0x8228D784, _mg, L(_mg))),
    (0x8228D788, ("lwz", 11, 4, 3)),
    (0x8228D78C, ("cmpwi6", 11, 0)),
    (0x8228D790, ("beq6", _mg, L(_mg))),
    (0x8228D794, ("lwz", 10, 12, 3)),
    (0x8228D798, ("lwz", 9, 276, 11)),
    (0x8228D79C, ("slwi", 10, 10, 2)),
    (0x8228D7A0, ("stwx", 31, 9, 10)),
    (0x8228D7A4, ("lwz", 11, 284, 11)),
    (0x8228D7A8, ("stwx", 31, 11, 10)),
    (0x8228D7AC, ("b", 0x8228D7AC, _mg, L(_mg))),
    (0x8228D7B0, ("bl", 0x8228D7B0, 0x8228D618)),
]
_cases = {off: E(_jb, off) for off in range(0, 0x34, 4)}
_cases.update({0x04: L(_mg), 0x2C: L(_mg), 0x34: L(_mg),
               0x38: E(_jb, 0x38), 0x3C: E(_jb, 0x3C), 0x40: E(_jb, 0x40),
               0x44: E(_jb, 0x44), 0x48: L(0x8228D7C8),
               0x4C: L(0x8228D7CC), 0x50: E(_jb, 0x50)})
_ins = tail_ins(_jb, [
    (0x38, *ADDI(30, 2)), (0x3C, *ADDI(29, 4)), (0x40, *CMPW(27, 26)),
    (0x44, *BNE(0x8228D740)), (0x50, *REST(25))])
SPECS.append(dict(file="pacificrim_recomp.35.cpp", func=0x8228D6F0,
                  jb=_jb, merge=_mg, region=_reg, cases=_cases, ins=_ins))

# --- S7 sub_8229D728 (recomp.55), pair-chain ---
_jb, _mg = 0x8229D7B4, 0x8229D7F0
_reg, _cases = pair_chain(_jb, _mg, 0x8228D078, 0x82288188, 0x8228D538)
_cases.update({0x3C: L(_mg), 0x40: E(_jb, 0x40), 0x44: E(_jb, 0x44),
               0x48: E(_jb, 0x48), 0x4C: E(_jb, 0x4C),
               0x50: L(0x8229D804), 0x54: E(_jb, 0x54)})
_ins = tail_ins(_jb, [
    (0x40, *ADDI(31, 2)), (0x44, *ADDI(30, 4)), (0x48, *CMPW(28, 27)),
    (0x4C, *BNE(0x8229D774)), (0x54, *REST(26, "0x823ab4c0"))])
SPECS.append(dict(file="pacificrim_recomp.55.cpp", func=0x8229D728,
                  jb=_jb, merge=_mg, region=_reg, cases=_cases, ins=_ins))

# --- S8/S9 vtable-call walkers ---
def _vtable_spec(fname, func, jb, mg, loop):
    reg = [
        (jb + 0x00, ("bl", jb + 0x00, 0x8228DF20)),
        (jb + 0x04, ("b", jb + 0x04, mg, L(mg))),
        (jb + 0x08, ("lwz", 11, 0, 3)),
        (jb + 0x0C, ("li", 4, 0)),
        (jb + 0x10, ("lwz", 11, 0, 11)),
        (jb + 0x14, ("mtctr", 11)),
        (jb + 0x18, ("bctrl", jb + 0x18)),
        (jb + 0x1C, ("b", jb + 0x1C, mg, L(mg))),
        (jb + 0x20, ("bl", jb + 0x20, 0x822D3288)),
    ]
    cases = {off: E(jb, off) for off in range(0, 0x24, 4)}
    cases.update({0x04: L(mg), 0x1C: L(mg), 0x24: L(mg),
                  0x28: E(jb, 0x28), 0x2C: E(jb, 0x2C),
                  0x30: E(jb, 0x30), 0x34: E(jb, 0x34)})
    ins = tail_ins(jb, [
        (0x28, *ADDI(31, 2)), (0x2C, *ADDI(30, 4)), (0x30, *CMPW(28, 27)),
        (0x34, *BNE(loop))])
    return dict(file=fname, func=func, jb=jb, merge=mg, region=reg,
                cases=cases, ins=ins)


SPECS.append(_vtable_spec("pacificrim_recomp.56.cpp", 0x822F7248,
                          0x822F72D4, 0x822F72F8, 0x822F7294))
SPECS.append(_vtable_spec("pacificrim_recomp.30.cpp", 0x822F73F0,
                          0x822F747C, 0x822F74A0, 0x822F743C))

# --- S10/S11 dual-mr chains ---
def _dual_spec(fname, func, jb, mg, loop, exit_va):
    reg = []
    for i, t in enumerate([0x822F9248, 0x822F8AD8, 0x822F8B80, 0x822F8C28,
                           0x822D2F10, 0x822F8CD0, 0x822F8D78]):
        a = jb + 16 * i
        reg += [(a, ("mr", 4, 27)), (a + 4, ("mr", 3, 31)),
                (a + 8, ("bl", a + 8, t)),
                (a + 12, ("b", a + 12, mg, L(mg)))]
    a = jb + 0x70
    reg += [(a, ("mr", 4, 27)), (a + 4, ("mr", 3, 31)),
            (a + 8, ("bl", a + 8, 0x822F92D0)),
            (a + 12, ("lwz", 11, 0, 31)),
            (a + 16, ("lbz", 11, 44, 11)),
            (a + 20, ("cmplwi0", 11, 0)),
            (a + 24, ("bne0", mg, L(mg))),
            (a + 28, ("mr", 3, 31)),
            (a + 32, ("bl", a + 32, 0x8228D618))]
    cases = {off: E(jb, off) for off in range(0, 0x94, 4)}
    cases.update({0x94: L(mg), 0x98: E(jb, 0x98), 0x9C: E(jb, 0x9C),
                  0xA0: E(jb, 0xA0), 0xA4: E(jb, 0xA4),
                  0xA8: L(exit_va), 0xAC: E(jb, 0xAC)})
    ins = tail_ins(jb, [
        (0x98, *ADDI(30, 2)), (0x9C, *ADDI(29, 4)), (0xA0, *CMPW(26, 25)),
        (0xA4, *BNE(loop)),
        (0xAC, "\t// addi r1,r1,160", "\tctx.r1.s64 = ctx.r1.s64 + 160;"),
        ])
    # 0xAC entry is the restgprlr line itself; fix anchor below in apply
    ins[-1] = ("\t// b 0x823ab4b8", "\t__restgprlr_24(ctx, base);",
               E(jb, 0xAC))
    return dict(file=fname, func=func, jb=jb, merge=mg, region=reg,
                cases=cases, ins=ins)


SPECS.append(_dual_spec("pacificrim_recomp.44.cpp", 0x822F8E20,
                        0x822F8EB0, 0x822F8F44, 0x822F8E70, 0x822F8F58))
SPECS.append(_dual_spec("pacificrim_recomp.45.cpp", 0x822F94D8,
                        0x822F9568, 0x822F95FC, 0x822F9528, 0x822F9610))

# --- S12/S13 mr-chains (822F family) ---
def _mr_spec(fname, func, jb, mg, loop, exit_va, first):
    reg, cases = mr_chain(jb, mg, [first, 0x822F8F60, 0x822F8FB8,
                                   0x822F9010, 0x8229D950, 0x822F9068,
                                   0x822C2B98, 0x822F90C0])
    cases.update({0x5C: L(mg), 0x60: E(jb, 0x60), 0x64: E(jb, 0x64),
                  0x68: E(jb, 0x68), 0x6C: E(jb, 0x6C),
                  0x70: L(exit_va), 0x74: E(jb, 0x74)})
    ins = tail_ins(jb, [
        (0x60, *ADDI(31, 2)), (0x64, *ADDI(30, 4)), (0x68, *CMPW(27, 26)),
        (0x6C, *BNE(loop)), (0x74, *REST(25))])
    return dict(file=fname, func=func, jb=jb, merge=mg, region=reg,
                cases=cases, ins=ins)


SPECS.append(_mr_spec("pacificrim_recomp.72.cpp", 0x822F93D0,
                      0x822F9460, 0x822F94BC, 0x822F9420, 0x822F94D0,
                      0x822F93D0))
SPECS.append(_mr_spec("pacificrim_recomp.30.cpp", 0x822F9618,
                      0x822F96A8, 0x822F9704, 0x822F9668, 0x822F9718,
                      0x822F93D0))

# ------------------------------------------------------------ apply
hdr_of = {}
for _h in glob.glob(GEN + "/pacificrim_funcs*.h"):
    _txt = open(_h).read()
    for _m in re.finditer(r"sub_([0-9A-F]{8})", _txt):
        hdr_of.setdefault(int(_m.group(1), 16), _h.split("/")[-1])


def span_of(src, func):
    i = src.find(f"DEFINE_REX_FUNC(sub_{func:08X})")
    if i == -1:
        raise SystemExit(f"ABORT: sub_{func:08X} not found")
    j = src.find("DEFINE_REX_FUNC", i + 10)
    return i, (j if j != -1 else len(src))


for spec in SPECS:
    path = f"{GEN}/{spec['file']}"
    src = open(path).read()
    jb, mg = spec["jb"], spec["merge"]
    fresh = E(jb, 0) not in src
    if fresh:
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
        # tail entry labels, anchored inside this function's span
        i, j = span_of(src, spec["func"])
        span = src[i:j]
        for comment, stmt, label in spec["ins"]:
            anchor = comment + "\n" + stmt
            if span.count(anchor) != 1:
                raise SystemExit(
                    f"ABORT: tail anchor x{span.count(anchor)} for "
                    f"sub_{spec['func']:08X}: {comment!r}")
            span = span.replace(anchor, comment + "\n" + label + ":\n" + stmt)
        src = src[:i] + span + src[j:]
    # includes for new call targets
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
    open(path, "w").write(src)
    print(("patched" if fresh else "repaired") +
          f" sub_{spec['func']:08X} in {spec['file']}")
print("cluster2 patch complete")
