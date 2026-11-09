import sys, hashlib
a = open(sys.argv[1],'rb').read()
b = open(sys.argv[2],'rb').read()
print("2号 %d 字节  %s" % (len(a), hashlib.sha256(a).hexdigest()[:16]))
print("3号 %d 字节  %s" % (len(b), hashlib.sha256(b).hexdigest()[:16]))
n = min(len(a), len(b))
diff = [i for i in range(n) if a[i] != b[i]]
print("不同字节数: %d / %d" % (len(diff), n))
if diff:
    # 归并成区间
    runs = []
    start = prev = diff[0]
    for i in diff[1:]:
        if i == prev + 1:
            prev = i
        else:
            runs.append((start, prev)); start = prev = i
    runs.append((start, prev))
    print("差异区间 (%d 段):" % len(runs))
    for s, e in runs[:12]:
        print("  0x%05X - 0x%05X  (%d 字节)" % (s, e, e-s+1))
        print("    2号: %s" % a[s:min(e+1, s+16)].hex(' '))
        print("    3号: %s" % b[s:min(e+1, s+16)].hex(' '))
