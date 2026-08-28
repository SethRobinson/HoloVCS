# holo_gif.py - assemble looping GIFs from the 3DS core's packed multiview quilt dumps.
# Driven by tools/make_holo_gif.ps1; see docs/automation_workflow.md.
#
# A packed quilt (holo_quiltpk_NN.bmp) is the exact ABI buffer: cols x rows landscape
# 400x240 views, view 0 at the BOTTOM-left of the image, row-major upward (Looking Glass
# tile order). View count = cols * rows exactly, so the grid auto-detects from the size.
#
# Modes:
#   sweep   --quilt <bmp>      one quilt -> seamless ping-pong view sweep (a wigglegram:
#                              frozen moment, the viewpoint slides left-right-left)
#   frames  --framedir <dir>   quilt_0000.bmp.. captured over time (e.g. a cutaway ramp);
#                              frame k shows view v(k) of quilt k, so the view sweeps
#                              WHILE the captured effect animates
import argparse
import math
import os
import sys

import numpy as np
from PIL import Image

VIEW_W, VIEW_H = 400, 240  # native 3DS top screen


def slice_quilt(path):
    """Return the list of per-view images, view 0 = leftmost of the sweep."""
    img = Image.open(path).convert("RGB")
    cols = img.width // VIEW_W
    rows = img.height // VIEW_H
    if cols * VIEW_W != img.width or rows * VIEW_H != img.height:
        sys.exit(f"ERROR: {path} is {img.width}x{img.height}, not a whole grid of {VIEW_W}x{VIEW_H} tiles")
    views = []
    for v in range(cols * rows):
        col = v % cols
        row = v // cols                      # row 0 = bottom of the image
        y0 = (rows - 1 - row) * VIEW_H       # PIL origin is top-left
        views.append(img.crop((col * VIEW_W, y0, (col + 1) * VIEW_W, y0 + VIEW_H)))
    return views


def measure_edge_black(views):
    """Max run of near-black pixels from each edge, over every view: the per-view shear
    leaves unrendered black margins on the outer views, widest where the scene is deep.
    Returns (left, top, right, bottom) crop amounts, each capped at 40px."""
    l = t = r = b = 0
    for img in views:
        a = np.asarray(img.convert("L")) < 6
        lit = ~a
        first_lit_x = np.where(lit.any(axis=1), lit.argmax(axis=1), 0)
        last_lit_x = np.where(lit.any(axis=1),
                              a.shape[1] - 1 - lit[:, ::-1].argmax(axis=1), a.shape[1] - 1)
        l = max(l, int(first_lit_x.max()))
        r = max(r, int(a.shape[1] - 1 - last_lit_x.min()))
    # top/bottom measured INSIDE the x-crop: the side wedges' mostly-black columns would
    # otherwise read as huge top runs and blow the vertical crop out to the cap
    xl = min(40, l + 2 if l else 0)
    xr = min(40, r + 2 if r else 0)
    for img in views:
        a = np.asarray(img.convert("L")) < 6
        a = a[:, xl:a.shape[1] - xr]
        lit = ~a
        first_lit_y = np.where(lit.any(axis=0), lit.argmax(axis=0), 0)
        last_lit_y = np.where(lit.any(axis=0),
                              a.shape[0] - 1 - lit[::-1, :].argmax(axis=0), a.shape[0] - 1)
        t = max(t, int(first_lit_y.max()))
        b = max(b, int(a.shape[0] - 1 - last_lit_y.min()))
    return (xl, min(40, t + 2 if t else 0), xr, min(40, b + 2 if b else 0))


def view_index(frame, total_frames, view_count, cycles):
    """Sinusoidal sweep centered on the middle view; whole cycles = seamless loop."""
    phase = 2.0 * math.pi * cycles * frame / total_frames
    center = (view_count - 1) / 2.0
    return int(round(center + center * math.sin(phase)))


def build_frames(args):
    frames = []
    crop = (0, 0, 0, 0)
    if args.quilt:
        views = slice_quilt(args.quilt)
        if args.autocrop:
            crop = measure_edge_black(views)
        total = max(2, int(round(args.seconds * args.fps)))
        for k in range(total):
            frames.append(views[view_index(k, total, len(views), args.cycles)])
    else:
        files = sorted(f for f in os.listdir(args.framedir)
                       if f.startswith("quilt_") and f.lower().endswith(".bmp"))
        if not files:
            sys.exit(f"ERROR: no quilt_*.bmp in {args.framedir}")
        if args.autocrop:
            # measure on the first capture only: a cutaway can blacken large areas
            # mid-sequence, which must not inflate the crop
            crop = measure_edge_black(slice_quilt(os.path.join(args.framedir, files[0])))
        total = len(files)
        for k, f in enumerate(files):
            views = slice_quilt(os.path.join(args.framedir, f))
            frames.append(views[view_index(k, total, len(views), args.cycles)])
    if any(crop):
        l, t, r, b = crop
        print(f"autocrop: trimming L{l} T{t} R{r} B{b} (unrendered shear margins)")
        frames = [f.crop((l, t, f.width - r, f.height - b)) for f in frames]
    return frames


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--quilt", help="single packed quilt bmp (sweep mode)")
    src.add_argument("--framedir", help="dir of quilt_NNNN.bmp captured over time (frames mode)")
    ap.add_argument("--out", required=True, help="output .gif path")
    ap.add_argument("--fps", type=float, default=15.0)
    ap.add_argument("--seconds", type=float, default=6.0, help="sweep mode: GIF length")
    ap.add_argument("--cycles", type=float, default=1.0, help="view-sweep sine cycles over the loop")
    ap.add_argument("--scale", type=int, default=2, help="nearest-neighbor upscale factor")
    ap.add_argument("--colors", type=int, default=256)
    ap.add_argument("--autocrop", action=argparse.BooleanOptionalAction, default=True,
                    help="trim the outer views' unrendered black edge margins (measured, capped 40px)")
    args = ap.parse_args()

    frames = build_frames(args)
    if args.scale != 1:
        frames = [f.resize((f.width * args.scale, f.height * args.scale), Image.NEAREST)
                  for f in frames]

    # One global palette (built from a montage of sample frames) so the palette cannot
    # flicker frame to frame.
    samples = [frames[0], frames[len(frames) // 4], frames[len(frames) // 2],
               frames[(3 * len(frames)) // 4]]
    montage = Image.new("RGB", (samples[0].width, samples[0].height * len(samples)))
    for i, s in enumerate(samples):
        montage.paste(s, (0, i * s.height))
    palette_img = montage.quantize(colors=args.colors, method=Image.MEDIANCUT)
    frames = [f.quantize(palette=palette_img, dither=Image.FLOYDSTEINBERG) for f in frames]

    duration_ms = int(round(1000.0 / args.fps))
    frames[0].save(args.out, save_all=True, append_images=frames[1:],
                   duration=duration_ms, loop=0, optimize=True)
    size_mb = os.path.getsize(args.out) / (1024.0 * 1024.0)
    print(f"wrote {args.out}: {len(frames)} frames, {frames[0].width}x{frames[0].height}, "
          f"{args.fps:g} fps, {size_mb:.1f} MB")


if __name__ == "__main__":
    main()
