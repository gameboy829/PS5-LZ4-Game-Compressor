from pathlib import Path
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
BLUE="#6da5ff"
BG="#101827"
svg='''<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512" role="img" aria-label="LZ4 Game Compressor">
<rect width="512" height="512" fill="#101827"/>
<g fill="#6da5ff">
<path d="M40 230h87l-37-46 31-25 65 81q13 16 0 32l-65 81-31-25 37-46H40z"/>
<path d="M472 230h-87l37-46-31-25-65 81q-13 16 0 32l65 81 31-25-37-46h87z"/>
<path d="M220 80h72q-38 176 0 352h-72q38-176 0-352z"/>
</g></svg>'''
(ROOT/'assets/lz4-logo.svg').write_text(svg,encoding='utf-8')
scale=4
image=Image.new('RGB',(512*scale,512*scale),BG);draw=ImageDraw.Draw(image)
def poly(points):draw.polygon([(round(x*scale),round(y*scale)) for x,y in points],fill=BLUE)
def quad(a,b,c):
 return [((1-t)**2*a[0]+2*(1-t)*t*b[0]+t*t*c[0],(1-t)**2*a[1]+2*(1-t)*t*b[1]+t*t*c[1]) for t in [i/64 for i in range(65)]]
left=[(40,230),(127,230),(90,184),(121,159),(186,240)]+quad((186,240),(199,256),(186,272))+[(121,353),(90,328),(127,282),(40,282)]
poly(left);poly([(512-x,y) for x,y in left])
poly([(220,80),(292,80)]+quad((292,80),(254,256),(292,432))+[(220,432)]+quad((220,432),(258,256),(220,80)))
image=image.resize((512,512),Image.Resampling.LANCZOS)
image.save(ROOT/'assets-app/icon0.png');image.save(ROOT/'LZ4-LOGO.png')
print('Created simple blue compression icon')
