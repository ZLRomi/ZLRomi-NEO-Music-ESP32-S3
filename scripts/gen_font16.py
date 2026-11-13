#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_font16.py —— 生成 16x16 小号点阵字库（4bpp 抗锯齿）

为什么需要：
    现有字库是 20x24 的汉字（font_cn.bin，1.5MB），横屏 320 宽一行只放得下 16 个字，
    歌名/歌手这种长文本显示不下。这里再生成一套 16x16 的小字：
    汉字 16 宽、ASCII 9 宽，一行能放 20 个汉字，行高也从 24 降到 16。

字库格式（与 font_data.c 对应）：
    每个字形按行存储，每行 ceil(宽/2) 字节，4bpp，高 nibble 是左边那个像素。
    汉字 : 16 行 x 8 字节 = 128 字节/字
    ASCII: 16 行 x 5 字节 =  80 字节/字

码位表直接复用 font_index.c 里的 g_cn_codepoints，保证与小字库一一对应。

用法：
    python scripts/gen_font16.py
"""

import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(ROOT, "main")
INDEX_C = os.path.join(MAIN, "src", "font_index.c")

CN_FONT = "C:/Windows/Fonts/msyh.ttc"      # 微软雅黑
ASCII_FONT = "C:/Windows/Fonts/consola.ttf"  # Consolas 等宽

CN_SIZE = 16
CN_W, CN_H = 16, 16
ASCII_SIZE = 16
ASCII_W, ASCII_H = 9, 16

CN_OUT = os.path.join(MAIN, "font_cn16.bin")
ASCII_OUT = os.path.join(MAIN, "font_ascii16.bin")


def load_codepoints():
    """从 font_index.c 解析小字库码位表 g_cn16_codepoints（含标点），
    顺序必须与 font_cn16.bin 完全一致。
    不能用 g_cn_codepoints —— 那套是纯 GB2312 汉字，没有标点，
    歌名里的中文逗号/句号/冒号会全部显示成方块。"""
    with open(INDEX_C, encoding="utf-8") as fh:
        src = fh.read()
    m = re.search(r"g_cn16_codepoints\[\d+\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        sys.exit("无法在 font_index.c 里找到 g_cn16_codepoints，"
                 "请先运行过一次码位表更新")
    body = m.group(1)
    cps = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4})", body)]
    return cps


def pack(img, width, height):
    """把 L 模式灰度图打包成 4bpp：高 nibble 是左像素"""
    out = bytearray()
    stride = (width + 1) // 2
    for y in range(height):
        row = bytearray(stride)
        for x in range(width):
            a = img.getpixel((x, y)) >> 4      # 0..15
            if x & 1:
                row[x >> 1] |= a
            else:
                row[x >> 1] = a << 4
        out += row
    return bytes(out)


def render_cn(codepoints):
    """雅黑 16px 渲染；墨迹超出 16x16 的字自动降一号重画，避免被裁掉"""
    glyph_bytes = CN_W * CN_H // 2
    out = {}
    pending = list(codepoints)
    size = CN_SIZE

    while pending and size >= 11:
        font = ImageFont.truetype(CN_FONT, size)
        retry = []
        for cp in pending:
            ch = chr(cp)
            probe = Image.new("L", (CN_SIZE * 2, CN_SIZE * 2), 0)
            ImageDraw.Draw(probe).text((0, 0), ch, font=font, fill=255)
            bbox = probe.getbbox()
            if bbox is None:
                out[cp] = b"\x00" * glyph_bytes
                continue
            bw, bh = bbox[2] - bbox[0], bbox[3] - bbox[1]
            if bw > CN_W or bh > CN_H:
                retry.append(cp)
                continue
            x_off = (CN_W - bw) // 2 - bbox[0]
            y_off = (CN_H - bh) // 2 - bbox[1]
            img = Image.new("L", (CN_W, CN_H), 0)
            ImageDraw.Draw(img).text((x_off, y_off), ch, font=font, fill=255)
            out[cp] = pack(img, CN_W, CN_H)
        if retry and size > 11:
            print(f"  {size}px 下有 {len(retry)} 个字超界，降到 {size - 1}px 重画")
        pending = retry
        size -= 1

    for cp in pending:                     # 还是超界的（极少见），按空字处理
        out[cp] = b"\x00" * glyph_bytes

    buf = bytearray()
    for cp in codepoints:
        buf += out[cp]
    return bytes(buf)


def render_ascii():
    font = ImageFont.truetype(ASCII_FONT, ASCII_SIZE)
    buf = bytearray()
    for code in range(32, 127):
        ch = chr(code)
        probe = Image.new("L", (ASCII_SIZE * 2, ASCII_SIZE * 2), 0)
        ImageDraw.Draw(probe).text((0, 0), ch, font=font, fill=255)
        bbox = probe.getbbox()
        if bbox is None:
            buf += b"\x00" * (((ASCII_W + 1) // 2) * ASCII_H)
            continue
        x_off = 0                       # 等宽字体左对齐
        y_off = (ASCII_H - (bbox[3] - bbox[1])) // 2 - bbox[1]
        img = Image.new("L", (ASCII_W, ASCII_H), 0)
        ImageDraw.Draw(img).text((x_off, y_off), ch, font=font, fill=255)
        buf += pack(img, ASCII_W, ASCII_H)
    return bytes(buf)


def preview(path, width, height, chars):
    """把几个字反解出来打印成 #/· 便于肉眼确认"""
    with open(path, "rb") as fh:
        data = fh.read()
    stride = (width + 1) // 2
    print(f"--- {os.path.basename(path)} 预览 ---")
    for ch in chars:
        idx = 0
        if path == CN_OUT:
            cps = load_codepoints()
            idx = cps.index(ord(ch))
        else:
            idx = ord(ch) - 32
        off = idx * stride * height
        print(f"[{ch}]")
        for y in range(height):
            row = data[off + y * stride: off + (y + 1) * stride]
            line = ""
            for x in range(width):
                b = row[x >> 1]
                a = (b & 0x0F) if (x & 1) else (b >> 4)
                line += "#" if a >= 6 else ("+" if a >= 2 else ".")
            print("   " + line)


if __name__ == "__main__":
    cps = load_codepoints()
    print(f"码位表: {len(cps)} 个汉字")

    cn = render_cn(cps)
    with open(CN_OUT, "wb") as fh:
        fh.write(cn)
    print(f"写出 {CN_OUT}: {len(cn)} 字节 ({len(cn) / 1024:.0f} KB)")

    asc = render_ascii()
    with open(ASCII_OUT, "wb") as fh:
        fh.write(asc)
    print(f"写出 {ASCII_OUT}: {len(asc)} 字节")

    preview(CN_OUT, CN_W, CN_H, ["风", "歌", "永"])
    preview(ASCII_OUT, ASCII_W, ASCII_H, ["A", "g", "8"])
