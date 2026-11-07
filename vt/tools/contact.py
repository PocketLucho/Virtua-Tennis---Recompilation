import sys, glob
from PIL import Image
d, out = sys.argv[1], sys.argv[2]
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 3
w = int(sys.argv[4]) if len(sys.argv) > 4 else 427
fs = sorted(glob.glob(d + '/frame_*.ppm'))
ims = [Image.open(f) for f in fs]
h = int(w * ims[0].size[1] / ims[0].size[0])
W = Image.new('RGB', (w * cols, h * ((len(ims) + cols - 1) // cols)))
for i, im in enumerate(ims):
    W.paste(im.resize((w, h)), ((i % cols) * w, (i // cols) * h))
W.save(out)
print(len(fs), [f[-9:-4] for f in fs])
