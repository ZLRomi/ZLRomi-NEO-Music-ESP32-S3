
import json, urllib.request, time, ssl
UA = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/120 Safari/537.36", "Accept": "application/json"}
urls = [
    ("代理 推荐歌单", "https://netstartapi.zlromi.com/personalized?limit=4"),
    ("代理 每日新歌", "https://netstartapi.zlromi.com/personalized/newsong?limit=10"),
    ("60s 榜单列表", "https://60s.viki.moe/v2/ncm-rank/list"),
    ("vkeys 歌词", "https://api.vkeys.cn/v2/music/netease/lyric?id=1"),
    ("代理 歌词", "https://netstartapi.zlromi.com/lyric?id=1"),
]
for name, u in urls:
    t0 = time.time()
    try:
        req = urllib.request.Request(u, headers=UA)
        body = urllib.request.urlopen(req, timeout=20).read()
        j = json.loads(body)
        code = j.get("code", "?")
        cnt = "?"
        for k in ("result", "data", "lrc"):
            v = j.get(k)
            if isinstance(v, list):
                cnt = len(v)
                break
        print(f"{name}: code={code} 项数={cnt} {len(body)}B {time.time()-t0:.2f}s")
    except Exception as e:
        print(f"{name}: FAIL {type(e).__name__} {str(e)[:80]}")
