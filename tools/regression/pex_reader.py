"""Read-only Skyrim PEX inspection; format cross-checked with Champollion FileReader."""
from pathlib import Path
import struct
import json

class Reader:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        self.pos = 0
        self.strings = []
    def value(self, fmt):
        out = struct.unpack_from(">" + fmt, self.data, self.pos)[0]
        self.pos += struct.calcsize(">" + fmt)
        return out
    def text(self):
        n = self.value("H")
        out = self.data[self.pos:self.pos+n].decode("utf8")
        self.pos += n
        return out
    def index(self):
        return self.strings[self.value("H")]
    def arg(self):
        kind = self.value("B")
        if kind == 0: return None
        if kind in (1, 2): return self.index()
        if kind == 3: return self.value("i")
        if kind == 4: return self.value("f")
        if kind == 5: return bool(self.value("B"))
        raise ValueError(kind)
    def function(self):
        ret, doc, flags, kind = self.index(), self.index(), self.value("I"), self.value("B")
        params = [(self.index(), self.index()) for _ in range(self.value("H"))]
        locals_ = [(self.index(), self.index()) for _ in range(self.value("H"))]
        code = []
        for i in range(self.value("H")):
            op = self.value("B")
            name, count, variadic = OPS[op]
            args = [self.arg() for _ in range(count)]
            if variadic: args += [[self.arg() for _ in range(self.arg())]]
            code.append([i, name, args])
        return {"params":params, "locals":locals_, "code":code}
    def read(self):
        assert self.value("I") == 0xFA57C0DE
        version = self.value("B"), self.value("B"), self.value("H")
        self.value("Q")
        source, user, machine = self.text(), self.text(), self.text()
        self.strings = [self.text() for _ in range(self.value("H"))]
        if self.value("B"):
            self.value("Q")
            for _ in range(self.value("H")):
                self.index(); self.index(); self.index(); self.value("B")
                for _ in range(self.value("H")): self.value("H")
        for _ in range(self.value("H")): self.index(); self.value("B")
        objects = []
        for _ in range(self.value("H")):
            name = self.index(); self.value("I")
            parent, doc, flags, auto = self.index(), self.index(), self.value("I"), self.index()
            variables = []
            for _ in range(self.value("H")):
                n, t, f = self.index(), self.index(), self.value("I")
                variables.append([n,t,self.arg()])
            for _ in range(self.value("H")):
                n,t,d,f,k = self.index(),self.index(),self.index(),self.value("I"),self.value("B")
                if k & 4: self.index()
                else:
                    if k & 1: self.function()
                    if k & 2: self.function()
            functions = {}
            for _ in range(self.value("H")):
                state = self.index()
                for _ in range(self.value("H")):
                    fname = self.index()
                    functions[f"{state}/{fname}"] = self.function()
            objects.append({"name":name,"variables":variables,"functions":functions})
        assert self.pos == len(self.data), (self.pos,len(self.data))
        return objects

NAMES = "nop iadd fadd isub fsub imul fmul idiv fdiv imod not ineg fneg assign cast cmp_eq cmp_lt cmp_lte cmp_gt cmp_gte jmp jmpt jmpf callmethod callparent callstatic return strcat propget propset array_create array_length array_getelement array_setelement array_findelement array_rfindelement".split()
COUNTS = [0,3,3,3,3,3,3,3,3,3,2,2,2,2,2,3,3,3,3,3,1,2,2,3,2,3,1,3,3,3,2,2,3,3,4,4]
OPS = [(name,count,i in (23,24,25)) for i,(name,count) in enumerate(zip(NAMES,COUNTS))]
