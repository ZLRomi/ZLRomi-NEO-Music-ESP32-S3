
import json, urllib.request, zipfile, io, re
tok = open(r"C:\Users\Administrator\Desktop\GitHub Token.txt").read().strip()
hdr = {"Authorization": f"token {tok}", "Accept": "application/vnd.github+json"}
run = 43407453234
req = urllib.request.Request(f"https://api.github.com/repos/ZLRomi/ZLRomi-NEO-Music-ESP32-S3/actions/runs/{run}/logs", headers=hdr)
url = urllib.request.urlopen(req, timeout=60).geturl()
data = urllib.request.urlopen(urllib.request.Request(url, headers=hdr), timeout=120).read()
z = zipfile.ZipFile(io.BytesIO(data))
t = z.read("0_build.txt").decode("utf-8", "replace")
# 只找 error: 前缀的短行（真正的诊断行，不是编译命令行）
for l in t.split("\n"):
    ls = l.strip()
    if ls.startswith("error:") or "error:" in ls[-200:]:
        print(ls[:300])
    if "FAILED" in ls and "esp-idf/main" in ls:
        print("F>>>", ls[:120])
