#!/usr/bin/env python3
"""VLW サブセットフォントの生成 (SPEC §5.4)。

M5GFX の loadFont() が読む VLW 形式を、必要な文字だけに絞って書き出す。
ファームウェアには埋め込まず microSD に置く。

必要な文字:
  - ASCII 全域
  - Latin-1 Supplement  (Mönchengladbach / Alavés)
  - Latin Extended-A    (Beşiktaş / Kraków / Ferencváros)
  - src/core/messages.cpp に出てくる日本語

src/core/messages.cpp のテンプレートを変更したら、このスクリプトを
必ず再実行すること。文字リストは messages.cpp から自動抽出する。

日本語フォントは Latin Extended-A を持っていないことが多い (Noto Sans JP は
98文字欠けている)。--font は複数指定でき、先に書いたフォントを優先しつつ、
グリフを持たない文字は後続のフォントから補う。

使い方:
    pip install freetype-py
    python3 tools/make_vlw.py \
        --font NotoSansJP[wght].ttf \
        --font NotoSans[wdth,wght].ttf \
        --out sd/fonts

--strict を付けると、1文字でも欠けた時点で失敗する (CI 向け)。
"""

import argparse
import os
import re
import struct
import sys

# --- 収録する文字の決定 -------------------------------------------------

LATIN1 = [chr(c) for c in range(0x00A1, 0x0100)]
LATIN_EXT_A = [chr(c) for c in range(0x0100, 0x0180)]
ASCII = [chr(c) for c in range(0x20, 0x7F)]
EXTRA = list("…→℃")


def japanese_from_messages(path):
    """messages.cpp / messages.h の文字列リテラルから日本語を拾う。"""
    if not os.path.exists(path):
        return []
    text = open(path, encoding="utf-8").read()
    chars = set()
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', text):
        for ch in lit:
            if ord(ch) > 0x2E7F:  # CJK / かな / 全角約物
                chars.add(ch)
    return sorted(chars)


def build_charset(src_dir):
    chars = []
    chars += ASCII
    chars += LATIN1
    chars += LATIN_EXT_A
    chars += EXTRA
    for name in ("messages.cpp", "messages.h"):
        chars += japanese_from_messages(os.path.join(src_dir, "core", name))
    # 重複除去して安定した順序にする
    seen, out = set(), []
    for c in chars:
        if c not in seen:
            seen.add(c)
            out.append(c)
    return out


# --- VLW 書き出し -------------------------------------------------------
#
# VLW (Processing / TFT_eSPI / M5GFX 互換) のレイアウト:
#   header: 6 x int32 BE  [glyphCount, version(11), size, mboxY, ascent, descent]
#   glyph metrics: glyphCount x 7 x int32 BE
#       [codepoint, height, width, setWidth, topExtent, leftExtent, padding]
#   bitmaps: 各グリフの 8bit グレースケール (height x width) を metrics と同順で
#   末尾: フォント名 (ASCII, 長さ int32 BE + 文字列) — M5GFX は読み飛ばす


def render_glyphs(font_paths, size, chars):
    """先頭のフォントを優先し、グリフが無い文字は後続のフォントで補う。"""
    import freetype

    faces = []
    for path in font_paths:
        face = freetype.Face(path)
        face.set_pixel_sizes(0, size)
        faces.append(face)

    # 縦方向のメトリクスは主フォントに合わせる
    primary = faces[0]
    ascent = primary.size.ascender >> 6
    descent = -(primary.size.descender >> 6)

    glyphs = []
    missing = []
    from_fallback = 0
    for ch in chars:
        face = None
        for i, f in enumerate(faces):
            if f.get_char_index(ord(ch)) != 0:
                face = f
                if i > 0:
                    from_fallback += 1
                break
        if face is None:
            missing.append(ch)
            continue
        face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
        bmp = face.glyph.bitmap
        w, h = bmp.width, bmp.rows
        data = bytes(bmp.buffer) if w and h else b""
        glyphs.append(
            {
                "cp": ord(ch),
                "height": h,
                "width": w,
                "set_width": face.glyph.advance.x >> 6,
                "top": face.glyph.bitmap_top,
                "left": face.glyph.bitmap_left,
                "bitmap": data,
            }
        )
    return glyphs, ascent, descent, missing, from_fallback


def write_vlw(path, glyphs, size, ascent, descent, name="subset"):
    with open(path, "wb") as f:
        f.write(struct.pack(">6i", len(glyphs), 11, size, size, ascent, descent))
        for g in glyphs:
            f.write(
                struct.pack(
                    ">7i",
                    g["cp"],
                    g["height"],
                    g["width"],
                    g["set_width"],
                    g["top"],
                    g["left"],
                    0,
                )
            )
        for g in glyphs:
            f.write(g["bitmap"])
        raw = name.encode("ascii", "ignore")
        f.write(struct.pack(">i", len(raw)))
        f.write(raw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True, action="append",
                    help="SIL OFL の TTF/OTF。複数指定可、先勝ち")
    ap.add_argument("--out", default="sd/fonts")
    ap.add_argument("--sizes", default="20,28")
    ap.add_argument("--src", default="src", help="messages.cpp のあるディレクトリ")
    ap.add_argument("--name", default="board")
    ap.add_argument("--strict", action="store_true",
                    help="1文字でも欠けたら失敗する")
    args = ap.parse_args()

    chars = build_charset(args.src)
    print(f"charset: {len(chars)} glyphs")

    os.makedirs(args.out, exist_ok=True)
    failed = False
    for size in [int(s) for s in args.sizes.split(",")]:
        glyphs, ascent, descent, missing, fallback_n = render_glyphs(
            args.font, size, chars)
        if missing:
            # ここに残った文字は実機で欠字する。src/core/text_util.cpp の
            # is_supported_codepoint() と食い違っていないか確認すること。
            print(f"  ERROR: {len(missing)} chars not found in any font: "
                  f"{''.join(missing)}", file=sys.stderr)
            failed = True
        out = os.path.join(args.out, f"{args.name}_{size}.vlw")
        write_vlw(out, glyphs, size, ascent, descent, f"{args.name}{size}")
        print(f"  wrote {out}: {len(glyphs)} glyphs "
              f"({fallback_n} from fallback fonts), "
              f"{os.path.getsize(out)/1024:.0f} KB")

    if failed and args.strict:
        sys.exit(1)

    print("\nライセンス: SIL OFL のフォントを使い、OFL 全文を sd/fonts/OFL.txt "
          "として同梱すること。派生フォントのファイル名に元の書体名をそのまま "
          "使わないこと (§7.3)。")


if __name__ == "__main__":
    main()
