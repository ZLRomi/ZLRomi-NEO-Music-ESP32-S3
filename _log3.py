
import json, urllib.request, zipfile, io, re
tok = open(r"C:\Users\Administrator\Desktop\GitHub Token.txt").read().strip()
hdr = {"Authorization": f"token {tok}", "Accept": "application/vnd.github+json"}
run = 43407453234
req = urllib.request.Request(f"https://api.github.com/repos/ZLRomi/ZLRomi-NEO-Music-ESP32-S3/actions/runs/{run}/logs", headers=hdr)
url = urllib.request.urlopen(req, timeout=60).geturl()
data = urllib.request.urlopen(urllib.request.Request(url, headers=hdr), timeout=120).read()
z = zipfile.ZipFile(io.BytesIO(data))
t = z.read("0_build.txt").decode("utf-8", "replace")
lines = t.split("\n")
for i, l in enumerate(lines):
    if re.search(r"error:|error\[", l):
        print("L", i)
        print("\n".join(lines[max(0,i-2):i+3]))
        print("======")
