
import io, re
# 全面格式化审查（format-truncation / format-overflow 类隐患）
files = ["main/src/screen_songs.c","main/src/screen_home.c","main/src/netease.c","main/src/music_state.c","main/src/screen_player.c"]
for f in files:
    s = open(f, encoding="utf-8").read().split("\n")
    for i, l in enumerate(s):
        if "snprintf" in l:
            m = re.search(r"snprintf\(\s*(\w+)\s*,\s*sizeof\((\w+)\)\s*,", l)
            if m:
                # 找前面的声明
                name = m.group(1)
                decl = None
                for j in range(i, -1, -1):
                    dm = re.search(r"char\s+%s\s*\[\s*(\d+)\s*\]|char\s+%s\s*\[\s*SONG[\w+ ()*]*\s*\]" % (name, name), s[j])
                    if dm:
                        decl = s[j].strip()
                        break
                print(f"{f}:{i+1}  {l.strip()[:90]}")
                if decl: print(f"     decl: {decl[:70]}")
