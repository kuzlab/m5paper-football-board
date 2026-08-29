このディレクトリに VLW フォントを置く (SPEC §5.4)。

  board_20.vlw
  board_28.vlw

生成方法:
  pip install freetype-py
  python3 tools/make_vlw.py --font /path/to/NotoSansJP-Regular.ttf --out sd/fonts

*.vlw は .gitignore に入れてある。リポジトリにはフォント成果物を含めず、
各自が生成する。使用したフォントの OFL 全文をこのディレクトリに
OFL.txt として置くこと (§7.3)。

フォントが読めない場合、ファームウェアは内蔵の英数字フォントに
フォールバックし、固定エリアに FONT ERR を表示する。日本語のプチ情報は
表示されなくなるが、スコアは読める。
