"""Bundle mv.html, assets.js and Isolated.mp3 into one self-contained HTML player.

    python build.py        -> Isolated_MV.html
"""
import base64, json, os
here = os.path.dirname(os.path.abspath(__file__))
html = open(os.path.join(here, 'mv.html'), encoding='utf-8').read()
assets = open(os.path.join(here, 'assets.js'), encoding='utf-8').read()
audio = base64.b64encode(open(os.path.join(here, 'Isolated.mp3'), 'rb').read()).decode('ascii')
tag = '<script src="assets.js"></script>'
assert tag in html
html = html.replace(tag, f'<script>\n{assets}window.EMBED = {json.dumps({"audio": audio})};\n</script>', 1)
out = os.path.join(here, 'Isolated_MV.html')
open(out, 'w', encoding='utf-8').write(html)
print(f'wrote {out}  ({os.path.getsize(out) / 1e6:.1f} MB)')
