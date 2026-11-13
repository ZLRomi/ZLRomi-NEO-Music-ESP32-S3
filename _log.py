
import json, urllib.request
tok = open(r"C:\Users\Administrator\Desktop\GitHub Token.txt").read().strip()
hdr = {"Authorization": f"token {tok}", "Accept": "application/vnd.github+json"}
run = 43407453234
jobs = json.load(urllib.request.urlopen(urllib.request.Request(f"https://api.github.com/repos/ZLRomi/ZLRomi-NEO-Music-ESP32-S3/actions/runs/{run}/jobs", headers=hdr), timeout=30))
for j in jobs["jobs"]:
    print("JOB:", j["name"], j["conclusion"])
    for s in j["steps"]:
        if s["conclusion"] not in ("success", "skipped"):
            print("  STEP:", s["name"], s["conclusion"])
# 下载日志
import base64
logs = json.load(urllib.request.urlopen(urllib.request.Request(f"https://api.github.com/repos/ZLRomi/ZLRomi-NEO-Music-ESP32-S3/actions/runs/{run}/logs", headers=hdr), timeout=60))
