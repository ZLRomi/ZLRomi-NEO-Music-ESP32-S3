
import json, urllib.request, time
# 从桌面 GitHub Token.txt 读 token（只读使用，不打印）
tok = open(r"C:\Users\Administrator\Desktop\GitHub Token.txt").read().strip()
hdr = {"Authorization": f"token {tok}", "Accept": "application/vnd.github+json"}
base = "https://api.github.com/repos/ZLRomi/ZLRomi-NEO-Music-ESP32-S3/actions/runs"
for attempt in range(60):
    req = urllib.request.Request(base + "?per_page=1", headers=hdr)
    data = json.load(urllib.request.urlopen(req, timeout=20))
    runs = data.get("workflow_runs", [])
    if runs:
        r = runs[0]
        print(r["id"], r["status"], r.get("conclusion"), r["head_sha"][:7])
        if r["status"] == "completed":
            break
    time.sleep(10)
