import os, re
dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ws")
cls_re = re.compile(r"\b(?:class|struct)\s+(?:__declspec\([^)]*\)\s*)?(\w+)\b[^;{]*\{")
amp = re.compile(r"<\s*&\s*(?!\w+::)(\w+)\s*>")
n = 0
for root, _, files in os.walk(dst):
    for f in files:
        if not f.endswith((".h", ".cpp")): continue
        p = os.path.join(root, f); s = open(p, encoding="utf-8", errors="surrogateescape").read(); orig = s
        for m in re.finditer(r"Routes\(\)\s*(?:override\s*)?\{", s):
            if s[max(0, m.start()-2):m.start()] == "::": continue
            classes = [c for c in cls_re.finditer(s, 0, m.start())]
            if not classes: continue
            cls = classes[-1].group(1); start = m.end(); depth, i = 1, start
            while i < len(s) and depth: depth += {"{":1,"}":-1}.get(s[i],0); i += 1
            body = s[start:i]; nb, k = amp.subn(lambda mm: "<&%s::%s>" % (cls, mm.group(1)), body); n += k
            s = s[:start] + nb + s[i:]
        if s != orig: open(p, "w", encoding="utf-8", errors="surrogateescape").write(s)
print("qualified in-class", n)
