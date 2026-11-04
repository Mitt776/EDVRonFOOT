"""Read a Frontier crash minidump of the exit crash and print what identifies it.

The exit crash (docs\\shutdown-freelibrary-race-2026-09-21.md) executes at an
address that always ends in ...F780: the RSSetViewports handler of OpenXR
Toolkit sits at RVA 0x8F780 of its DLL, and the loader unloaded that DLL while
its Detours jump in d3d11.dll stayed in place. This tool prints, per dump:

- the exception code and address, its low 16 bits, and the module (loaded or
  unloaded) it falls in;
- rax - rcx (0x168 = slot 44, RSSetViewports, of the context's vtable cell);
- the nearest modules below and above the address in the module map;
- the return addresses on the faulting thread's stack that land in a module
  (the chain: d3d11.dll+0x2EC9C4 -> gameoverlayrenderer64.dll+0x71D05 ...).

    python tools\\exit_crash_dump.py                 newest dump in CrashDumps
    python tools\\exit_crash_dump.py <file.dmp>      that dump
    python tools\\exit_crash_dump.py --all           one line per dump
    python tools\\exit_crash_dump.py --self-test

Reads only; writes nothing.
"""

import glob
import os
import struct
import sys

DUMP_DIR = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Temp",
                        "Frontier Developments", "EliteDangerous", "CrashDumps")

S_THREADS, S_MODULES, S_MEMORY, S_EXCEPTION, S_MEMORY64, S_UNLOADED = 3, 4, 5, 6, 9, 14
REGS = "rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split()
CTX_REGS, CTX_RIP = 0x78, 0xF8


class Dump(object):
    def __init__(self, data):
        self.data = data
        sig, _ver, n, dirrva = struct.unpack_from("<IIII", data, 0)
        if sig != 0x504D444D:  # 'MDMP'
            raise ValueError("not a minidump")
        self.streams = {}
        for i in range(n):
            t, size, rva = struct.unpack_from("<III", data, dirrva + i * 12)
            self.streams.setdefault(t, (size, rva))
        self.mods = self._modules(S_MODULES)
        self.unloaded = self._modules(S_UNLOADED)
        self.mem = self._memory()
        self.exc = self._exception()

    def _str(self, rva):
        n = struct.unpack_from("<I", self.data, rva)[0]
        return self.data[rva + 4: rva + 4 + n].decode("utf-16le", "replace")

    def _modules(self, kind):
        if kind not in self.streams:
            return []
        _size, rva = self.streams[kind]
        if kind == S_MODULES:
            n = struct.unpack_from("<I", self.data, rva)[0]
            hs, es = 4, 108
        else:
            hs, es, n = struct.unpack_from("<III", self.data, rva)
        out = []
        for i in range(n):
            base, sz, _ck, _ts, nr = struct.unpack_from("<QIIII", self.data, rva + hs + i * es)
            out.append((base, sz, self._str(nr)))
        return sorted(out)

    def _memory(self):
        mem = []
        if S_MEMORY64 in self.streams:
            _size, rva = self.streams[S_MEMORY64]
            n, off = struct.unpack_from("<QQ", self.data, rva)
            for i in range(n):
                st, sz = struct.unpack_from("<QQ", self.data, rva + 16 + i * 16)
                mem.append((st, sz, off))
                off += sz
        if S_MEMORY in self.streams:
            _size, rva = self.streams[S_MEMORY]
            n = struct.unpack_from("<I", self.data, rva)[0]
            for i in range(n):
                st, sz, r = struct.unpack_from("<QII", self.data, rva + 4 + i * 16)
                mem.append((st, sz, r))
        return mem

    def _exception(self):
        if S_EXCEPTION not in self.streams:
            return None
        _size, rva = self.streams[S_EXCEPTION]
        tid = struct.unpack_from("<I", self.data, rva)[0]
        code, _fl, _rec, addr = struct.unpack_from("<IIQQ", self.data, rva + 8)
        csz, crva = struct.unpack_from("<II", self.data, rva + 8 + 152)
        ctx = self.data[crva: crva + csz]
        regs = dict(zip(REGS, struct.unpack_from("<16Q", ctx, CTX_REGS)))
        regs["rip"] = struct.unpack_from("<Q", ctx, CTX_RIP)[0]
        return {"tid": tid, "code": code, "addr": addr, "regs": regs}

    def read(self, a, n):
        for st, sz, off in self.mem:
            if st <= a and a + n <= st + sz:
                return self.data[off + a - st: off + a - st + n]
        return None

    def where(self, a):
        for b, s, nm in self.mods:
            if b <= a < b + s:
                return "%s+0x%X%s" % (leaf(nm), a - b, tag(nm))
        for b, s, nm in self.unloaded:
            if b <= a < b + s:
                return "UNLOADED %s+0x%X" % (leaf(nm), a - b)
        return None

    def neighbours(self, a):
        below = above = None
        for b, s, nm in self.mods:
            if b + s <= a:
                below = (b, s, nm)
            if b > a and above is None:
                above = (b, s, nm)
        return below, above

    def chain(self, limit=12, depth=0x4000):
        out = []
        rsp = self.exc["regs"]["rsp"]
        for k in range(0, depth, 8):
            b = self.read(rsp + k, 8)
            if b is None:
                break
            w = self.where(struct.unpack("<Q", b)[0])
            if w:
                out.append(w)
                if len(out) >= limit:
                    break
        return out


def leaf(path):
    return path.replace("/", "\\").split("\\")[-1]


def tag(path):
    p = path.lower().replace("/", "\\")
    if leaf(p) == "d3d11.dll":
        return " (system)" if "\\system32\\" in p else " (edvr proxy)"
    return ""


def report(d):
    lines = []
    e = d.exc
    if e is None:
        return ["no exception stream"]
    a = e["addr"]
    lines.append("exception 0x%08X at 0x%016X  low16=%04X  tid=%d" % (e["code"], a, a & 0xFFFF, e["tid"]))
    lines.append("  in: %s" % (d.where(a) or "no module"))
    r = e["regs"]
    lines.append("  rax-rcx=0x%X" % ((r["rax"] - r["rcx"]) & 0xFFFFFFFFFFFFFFFF))
    below, above = d.neighbours(a)
    if below:
        lines.append("  below: %s ends 0x%X (address is +0x%X past it)" % (
            leaf(below[2]), below[0] + below[1], a - below[0] - below[1]))
    if above:
        lines.append("  above: %s at 0x%X" % (leaf(above[2]), above[0]))
    for b, s, nm in d.unloaded:
        lines.append("  unloaded: %s 0x%X size 0x%X" % (leaf(nm), b, s))
    ch = d.chain()
    lines.append("  stack: " + (" -> ".join(ch) if ch else "(not readable)"))
    return lines


def one_line(path, d):
    e = d.exc
    name = os.path.basename(path)[:15]
    if e is None:
        return name + " no exception"
    r = e["regs"]
    return "%s 0x%08X 0x%016X low16=%04X rax-rcx=0x%X %s" % (
        name, e["code"], e["addr"], e["addr"] & 0xFFFF,
        (r["rax"] - r["rcx"]) & 0xFFFFFFFFFFFFFFFF, d.where(e["addr"]) or "no module")


def dumps(root):
    return sorted(glob.glob(os.path.join(root, "*", "*.dmp")), key=os.path.getmtime)


def load(path):
    with open(path, "rb") as f:
        return Dump(f.read())


# --- self-test: a synthetic minidump shaped like the exit crash ------------

def build_fixture(addr=0x7FFF1662F780, rax_minus_rcx=0x168, unloaded=None):
    """Return the bytes of a minimal minidump: modules, unloaded modules,
    exception with context, one memory range holding the stack."""
    mods = [(0x7FFF20000000, 0x400000, u"C:\\Windows\\System32\\d3d11.dll"),
            (0x7FFF10000000, 0x3000000, u"C:\\steam\\Products\\elite-dangerous-odyssey-64\\d3d11.dll"),
            (0x7FFF30000000, 0x200000, u"C:\\steam\\gameoverlayrenderer64.dll"),
            (0x7FFF16800000, 0x100000, u"C:\\Windows\\System32\\kernel32.dll")]
    unl = unloaded if unloaded is not None else [
        (addr - 0x8F780, 0x360000, u"C:\\Program Files\\OpenXR-Toolkit\\XR_APILAYER_MBUCCHIA_toolkit.dll")]
    rsp = 0x000000A000001000
    stack = struct.pack("<QQQQ", 0x1234, 0x7FFF10000000 + 0x2EC9C4, 0x55, 0x7FFF30000000 + 0x71D05)
    ctx = bytearray(0x4D0)
    rcx = 0x1F0000000
    regs = [rcx + rax_minus_rcx, rcx, 0, 0, rsp] + [0] * 11
    struct.pack_into("<16Q", ctx, CTX_REGS, *regs)
    struct.pack_into("<Q", ctx, CTX_RIP, addr)

    body = bytearray()
    base = 32 + 6 * 12  # header + directory

    def put(b):
        off = base + len(body)
        body.extend(b)
        return off

    def mstr(s):
        e = s.encode("utf-16le")
        return put(struct.pack("<I", len(e)) + e + b"\0\0")

    names = [mstr(n) for _b, _s, n in mods]
    unames = [mstr(n) for _b, _s, n in unl]
    ml = struct.pack("<I", len(mods)) + b"".join(
        struct.pack("<QIIII", b, s, 0, 0, nr) + b"\0" * (108 - 24) for (b, s, _n), nr in zip(mods, names))
    ml_rva = put(ml)
    ul = struct.pack("<III", 12, 24, len(unl)) + b"".join(
        struct.pack("<QIIII", b, s, 0, 0, nr) for (b, s, _n), nr in zip(unl, unames))
    ul_rva = put(ul)
    ctx_rva = put(bytes(ctx))
    stack_rva = put(stack)
    mem = struct.pack("<I", 1) + struct.pack("<QII", rsp, len(stack), stack_rva)
    mem_rva = put(mem)
    exc = bytearray(168)
    struct.pack_into("<IIIIQQ", exc, 0, 4242, 0, 0xC0000005, 0, 0, addr)
    struct.pack_into("<II", exc, 8 + 152, len(ctx), ctx_rva)
    exc_rva = put(bytes(exc))
    th = struct.pack("<I", 0)
    th_rva = put(th)

    dirs = [(S_THREADS, len(th), th_rva), (S_MODULES, len(ml), ml_rva), (S_UNLOADED, len(ul), ul_rva),
            (S_MEMORY, len(mem), mem_rva), (S_EXCEPTION, len(exc), exc_rva), (0, 0, 0)]
    head = struct.pack("<IIIIIIQ", 0x504D444D, 0xA793, len(dirs), 32, 0, 0, 0)
    return head + b"".join(struct.pack("<III", *d) for d in dirs) + bytes(body)


def self_test():
    fails = []

    def check(cond, what):
        if not cond:
            fails.append(what)

    d = Dump(build_fixture())
    text = "\n".join(report(d))
    check("low16=F780" in text, "low 16 bits F780: " + text)
    check("rax-rcx=0x168" in text, "rax-rcx 0x168")
    check("UNLOADED XR_APILAYER_MBUCCHIA_toolkit.dll+0x8F780" in text, "fault lands in the unloaded Toolkit at 0x8F780")
    check("d3d11.dll+0x2EC9C4 (edvr proxy) -> gameoverlayrenderer64.dll+0x71D05" in text, "stack chain: " + text)
    check("exception 0xC0000005" in text, "exception code")
    check("below: kernel32.dll" not in text and "below: d3d11.dll" in text, "module below the address")

    d2 = Dump(build_fixture(addr=0x7FFF1663A000, rax_minus_rcx=0x60, unloaded=[]))
    t2 = "\n".join(report(d2))
    check("in: no module" in t2, "address in no module")
    check("rax-rcx=0x60" in t2 and "low16=A000" in t2, "other signature reads as other")
    check("UNLOADED" not in one_line("x.dmp", d2), "one line without an unloaded module")
    check(d.where(0x7FFF20000010) == "d3d11.dll+0x10 (system)", "system d3d11 tagged")

    try:
        Dump(b"\0" * 64)
        check(False, "a non-minidump is refused")
    except ValueError:
        pass

    if fails:
        for f in fails:
            print("self-test FAILED: " + f)
        sys.exit(1)
    print("exit_crash_dump self-test OK")


def main(argv):
    if "--self-test" in argv:
        self_test()
        return 0
    if "--all" in argv:
        files = dumps(DUMP_DIR)
        for p in files:
            try:
                print(one_line(p, load(p)))
            except (ValueError, struct.error) as e:
                print("%s unreadable: %s" % (os.path.basename(p), e))
        print("%d dump(s) in %s" % (len(files), DUMP_DIR))
        return 0
    paths = [a for a in argv if not a.startswith("--")]
    if paths:
        path = paths[0]
    else:
        files = dumps(DUMP_DIR)
        if not files:
            print("no dumps in " + DUMP_DIR)
            return 1
        path = files[-1]
    print(path)
    for line in report(load(path)):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
