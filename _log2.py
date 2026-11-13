
import json, urllib.request, zipfile, io, re, os
tok = open(r"C:\Users\Administrator\Desktop\GitHub Token.txt").read().strip()
hdr = {"Authorization": f"token {tok}", "Accept": "application/vnd.github+json"}
run = 43407453234
req = urllib.request.Request(f"https://api.github.com/repos/ZLRomi/ZLRomi-NEO-Music-ESP32-S3/actions/runs/{run}/logs", headers=hdr)
url = urllib.request.urlopen(req, timeout=60).geturl()
data = urllib.request.urlopen(urllib.request.Request(url, headers=hdr), timeout=120).read()
z = zipfile.ZipFile(io.BytesIO(data))
print([n for n in z.namelist()])
for n in z.namelist():
    t = z.read(n).decode("utf-8", "replace")
    lines = t.split("\n")
    # 找 error 行
    for i, l in enumerate(lines):
        if re.search(r"error:|Error \d+|FAILED", l):
            print("----", n, "line", i)
            print("\n".join(lines[max(0,i-3):i+4]))
