import re
p = 'main/src/netease.c'
s = open(p, encoding='utf-8').read()

# 逐行配平
print("逐行配平检查：")
balance = 0
for i, line in enumerate(s.split('\n'), 1):
    code = re.sub(r'/\*.*?\*/', '', line)
    code = re.sub(r'//.*', '', code)
    # 简单去字符串
    while '"' in code:
        a = code.index('"')
        b = code.find('"', a+1)
        if b == -1: break
        code = code[:a] + '""' + code[b+1:]
    while "'" in code:
        a = code.index("'")
        b = code.find("'", a+1)
        if b == -1: break
        code = code[:a] + "''" + code[b+1:]
    opens = code.count('(')
    closes = code.count(')')
    if opens != closes:
        print("  Line %d: (=%d )=%d -> %s" % (i, opens, closes, line[:80]))
    balance += opens - closes
    if balance < 0:
        print("  Line %d: balance 负了 -> %s" % (i, line[:80]))
print("最终 balance:", balance)