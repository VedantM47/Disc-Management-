# Copies upstream/windirstat to spike/ws and qualifies &Handler -> &Class::Handler inside X::Routes() bodies.
import os, re, shutil, sys
src = r"C:\Users\imved\OneDrive\Desktop\OS_CP\upstream\windirstat"
dst = os.path.join(os.path.dirname(__file__), "ws")
if os.path.exists(dst): shutil.rmtree(dst)
shutil.copytree(src, dst)
head = re.compile(r"(\w+)::Routes\(\)\s*\{")
amp = re.compile(r"<\s*&\s*(?!\w+::)(\w+)\s*>")
changed = 0
for root, _, files in os.walk(dst):
    for f in files:
        if not f.endswith((".h", ".cpp")): continue
        p = os.path.join(root, f)
        s = open(p, encoding="utf-8-sig", errors="surrogateescape").read()
        out, pos = [], 0
        for m in head.finditer(s):
            cls, start = m.group(1), m.end()
            depth, i = 1, start
            while i < len(s) and depth:
                depth += {"{": 1, "}": -1}.get(s[i], 0); i += 1
            body = s[start:i]
            nb, n = amp.subn(lambda mm: "<&%s::%s>" % (cls, mm.group(1)), body)
            changed += n
            out.append(s[pos:start]); out.append(nb); pos = i
        out.append(s[pos:])
        ns = "".join(out)
        if ns != s: open(p, "w", encoding="utf-8", errors="surrogateescape").write(ns)
print("qualified", changed)
