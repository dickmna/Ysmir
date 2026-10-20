"""Execute the compiled Burst control flow with fake native calls and a controllable clock.

Usage: python test_storm_pex.py YsmirStormScript.pex report.json
This checks actual bytecode, NOT Skyrim VM scheduling, save-file stacks or rendering.
"""
from pathlib import Path
import copy
import hashlib
import json
import random
import sys
from pex_reader import Reader

pex=Path(sys.argv[1])
obj=Reader(pex).read()[0]
fn=obj["functions"]["/Burst"]
assert obj["name"]=="YsmirStormScript" and not obj["variables"]
assert len(fn["params"])==9
code=fn["code"]

class Burst:
    def __init__(self, words, name="enemy", seed=100):
        params=["bolt","source_"+name,"aim_"+name,name,"player",words,.12,.35,.25]
        self.vars=dict(zip((n for n,_ in fn["params"]),params))
        self.vars.update({n:None for n,_ in fn["locals"]})
        self.pc=0
        self.now=0.
        self.due=0.
        self.status=1
        self.calls=[]
        self.finished=0
        self.random=random.Random(seed)
    def read(self, v):
        return self.vars[v] if isinstance(v,str) and v in self.vars else v
    def run(self, now):
        assert now>=self.due
        self.now=now
        for _ in range(1000):
            if self.pc==len(code):return
            _,op,a=code[self.pc]
            old=self.pc
            self.pc+=1
            if op in ("assign","cast"):
                self.vars[a[0]]=self.read(a[1])
            elif op in ("iadd","cmp_lt","cmp_eq"):
                l,r=self.read(a[1]),self.read(a[2])
                self.vars[a[0]]=l+r if op=="iadd" else l<r if op=="cmp_lt" else l==r
            elif op=="jmp":
                self.pc=old+a[0]
            elif op in ("jmpf","jmpt"):
                if bool(self.read(a[0]))==(op=="jmpt"):self.pc=old+a[1]
            elif op=="callstatic":
                cls,name,dest,raw=a
                args=[self.read(x) for x in raw]
                if (cls,name)==("ysmir","StormReady"):
                    assert args==[self.vars["akSource"],self.vars["akTarget"]]
                    self.vars[dest]=self.status
                elif (cls,name)==("ysmir","StormFinished"):
                    assert args==[self.vars["akSource"]]
                    self.finished+=1
                elif (cls,name)==("utility","RandomFloat"):
                    self.vars[dest]=self.random.uniform(*args)
                elif (cls,name)==("utility","Wait"):
                    assert args[0]>0
                    self.due=now+args[0]
                    return
                else:raise AssertionError((cls,name))
            elif op=="callmethod":
                name,receiver,_,raw=a
                assert name=="RemoteCast" and self.read(receiver)=="bolt"
                args=[self.read(x) for x in raw]
                assert args==[self.vars[k] for k in ("akSource","akBlame","akAim")]
                self.calls.append((now,args))
            else:raise AssertionError(op)
        raise AssertionError("Bytecode failed to yield or finish")
    def complete(self):
        while not self.finished:self.run(self.due)

checks=[]
for words in (1,2,3):
    b=Burst(words);b.complete()
    assert len(b.calls)==words and b.finished==1
    gaps=[b.calls[i+1][0]-b.calls[i][0] for i in range(words-1)]
    assert all(.12<=g<=.35 for g in gaps)
    assert b.vars["strikes"]==words
    checks.append(f"{words} words: exact cast count, original arguments, sequential random gaps")
for count in (0,1,2):
    b=Burst(3)
    for _ in range(count):b.run(b.due)
    b.status=-1;b.complete()
    assert len(b.calls)==count and b.finished==1
checks.append("Cancellation before first, second or third cast stops the remaining burst")
b=Burst(3);b.status=0
b.run(0);b.run(.25);assert not b.calls and b.vars["strikes"]==0
b.status=1;b.run(.5);assert len(b.calls)==1
b.status=0;b.run(b.due);assert len(b.calls)==1
b.status=1;b.complete();assert len(b.calls)==3
checks.append("Pause/retry does not consume a strike")
a=Burst(3,"a",100);b=Burst(3,"b",200)
a.run(0);b.run(0)
assert a.due!=b.due
first,second=sorted((a,b),key=lambda x:x.due)
first.run(first.due);assert len(first.calls)==2 and len(second.calls)==1
second.complete();first.complete()
assert a.finished==b.finished==1 and len(a.calls)==len(b.calls)==3
checks.append("Independent interleaved stacks have different gaps and no shared completion barrier")
b=Burst(3);b.run(0);b.run(10)
assert len(b.calls)==2 and 10.12<=b.due<=10.35 and not b.finished
b.complete()
checks.append("Delayed VM wakeup makes one cast and schedules a fresh gap, never a catch-up burst")
b=Burst(3);b.run(0)
restored=copy.deepcopy(b);restored.complete()
assert len(restored.calls)==3 and restored.finished==1
checks.append("Suspended local counter resumes without replay in the bytecode harness; real save VM untested")
report={"passed":True,"cases":len(checks),"checks":checks,"instructionCount":len(code),
        "pexSha256":hashlib.sha256(pex.read_bytes()).hexdigest(),
        "boundary":"Compiled PEX control flow with simulated natives/time; NOT game VM or save-format validation"}
Path(sys.argv[2]).write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n","utf8")
print(json.dumps(report,ensure_ascii=False,indent=2))
