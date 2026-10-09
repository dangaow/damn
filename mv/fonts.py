"""从 Google Fonts 下载 MV 用到的字体，只取用到的字（子集），存到 fonts/，生成 fonts/fonts.css。

    python fonts.py        改了歌词或 mv.html 里的文字后重新跑一次

用到哪些字：lyrics.lrc、mv.html 和 js/*.js 里出现的所有字符（不算注释）。字体均为 SIL Open Font License。
"""
import os
import re
import urllib.parse
import urllib.request

here = os.path.dirname(os.path.abspath(__file__))
FAMILIES = [                      # (CSS 里的名字, Google Fonts 名字, 字重)
    ('Noto Sans SC', 'Noto Sans SC', 400),
    ('Noto Sans SC', 'Noto Sans SC', 900),
    ('Noto Serif SC', 'Noto Serif SC', 400),
    ('Noto Serif SC', 'Noto Serif SC', 900),
    ('UnifrakturMaguntia', 'UnifrakturMaguntia', 400),
    ('Anton', 'Anton', 400),
    ('Space Mono', 'Space Mono', 400),
    ('Silkscreen', 'Silkscreen', 700),
    ('Long Cang', 'Long Cang', 400),          # 封面 A：手指在雾上写的「谎话」
]
UA = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36'


def get(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': UA}), timeout=60).read()


def main():
    chars = set()
    files = ['lyrics.lrc', 'mv.html'] + [os.path.join('js', f) for f in os.listdir(os.path.join(here, 'js')) if f.endswith('.js')]
    for f in files:                                          # 注释里的字不会画出来，不算（字太多时 Google Fonts 不按 text 子集化，只会返回一堆分片）
        src = open(os.path.join(here, f), encoding='utf-8-sig').read()
        if f.endswith('.js'):
            src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
            src = re.sub(r'(?m)(^|[^:\'"`])//[^\n]*', r'\1', src)
        if f.endswith('.html'):
            src = re.sub(r'<!--.*?-->', '', src, flags=re.S)
        chars |= set(src)
    chars |= {chr(c) for c in range(0x20, 0x7f)}             # 英文、数字、标点全要
    text = ''.join(sorted(c for c in chars if c.isprintable()))
    out = os.path.join(here, 'fonts')
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))
    css = []
    for name, gname, wght in FAMILIES:
        q = urllib.parse.urlencode({'family': f'{gname}:wght@{wght}', 'text': text, 'display': 'block'})
        sheet = get('https://fonts.googleapis.com/css2?' + q).decode()
        urls = re.findall(r'url\((https://[^)]+)\)', sheet)
        assert len(urls) == 1, f'{gname} {wght}: 应该拿到 1 个子集字体，实际 {len(urls)} 个（字太多时 Google Fonts 会改成分片）'
        fn = f"{gname.replace(' ', '')}-{wght}.woff2"
        data = get(urls[0])
        open(os.path.join(out, fn), 'wb').write(data)
        css.append(f"@font-face {{ font-family: '{name}'; font-weight: {wght}; font-display: block; src: url('{fn}') format('woff2'); }}")
        print(f'{fn:32s} {len(data) / 1024:7.1f} KB')
    open(os.path.join(out, 'fonts.css'), 'w', encoding='utf-8').write('\n'.join(css) + '\n')
    print(f'共 {len(text)} 个字符')


if __name__ == '__main__':
    main()
