import sys, hashlib, re
a = open(sys.argv[1],'rb').read()   # 刷进机器的
b = open(sys.argv[2],'rb').read()   # 刚编的
print("刷机版 %d 字节  %s" % (len(a), hashlib.sha256(a).hexdigest()[:16]))
print("刚编版 %d 字节  %s" % (len(b), hashlib.sha256(b).hexdigest()[:16]))
n = min(len(a), len(b))
diff = [i for i in range(n) if a[i] != b[i]]
print("不同字节数: %d" % len(diff))
if diff:
    runs=[]; s=p=diff[0]
    for i in diff[1:]:
        if i==p+1: p=i
        else: runs.append((s,p)); s=p=i
    runs.append((s,p))
    print("差异区间 %d 段:" % len(runs))
    for s,e in runs:
        print("  0x%05X-0x%05X (%d 字节)" % (s,e,e-s+1))
        print("    刷机版: %r" % a[s:e+1])
        print("    刚编版: %r" % b[s:e+1])
