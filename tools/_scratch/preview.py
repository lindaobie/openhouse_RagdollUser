import re, sys, os, glob, hashlib
def describe(d):
    head = d[:65536].decode('ascii','replace')
    m = re.search(r'lhbs [A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d', head)
    if m: return ("lhbs 固件", m.group(0))
    m = re.search(r'20\d\d-\d\d-\d\d \d\d:\d\d:\d\d', head)
    if m: return ("原厂镜像", m.group(0))
    return ("未知镜像", "")
files = sorted(glob.glob(os.path.join(sys.argv[1], 'backups', '*.bin')), key=os.path.getmtime)
print("  %-3s %-40s %-10s %s" % ("序号","文件","类型","构建时间"))
for i, f in enumerate(files, 1):
    d = open(f,'rb').read()
    s = sum(int.from_bytes(d[j:j+4],'little') for j in range(0,32,4)) & 0xFFFFFFFF
    k, w = describe(d)
    print("  %-3d %-40s %-10s %s%s" % (i, os.path.basename(f), k, w, "" if s==0 else "  [校验异常]"))
