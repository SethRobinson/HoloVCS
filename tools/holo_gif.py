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


def edge_runs(dark, axis):
    """Per-line dark run length from each end along `axis` (1 = per row giving
    left/right runs, 0 = per column giving top/bottom runs). An all-dark line reads
    as the full extent from both ends."""
    lit = ~dark
    extent = dark.shape[axis]
    any_lit = lit.any(axis=axis)
    first = np.where(any_lit, lit.argmax(axis=axis), extent)
    last = np.where(any_lit, np.flip(lit, axis=axis).argmax(axis=axis), extent)
    return first, last


def visible_margin(margins, runs, lum_lines, extent, flip):
    """Largest per-line margin whose inward neighbor pixels are BRIGHT. A wedge that
    borders dark scene art is invisible and cropping it only eats real content; the
    title's wedge against the glowing nebula is the one worth trimming. `lum_lines`
    indexes luminance per line; `flip` measures from the far end."""
    best = 0
    for i in np.nonzero(margins)[0]:
        run = int(runs[i])
        if flip:
            nb = lum_lines(i)[max(0, extent - run - 8):extent - run]
        else:
            nb = lum_lines(i)[run:run + 8]
        if nb.size and nb.mean() > 40:
            best = max(best, int(margins[i]))
    return best


def measure_edge_black(views):
    """Crop for the unrendered black shear margins on the OUTER views. Three filters,
    each learned from a real scene: subtract the CENTER view's run on the same line as
    a baseline (the center has no margins by construction; Metroid's ruins pillars are
    exact-zero black scene art), require the margin's inward neighbors to be bright
    (a wedge hiding against dark stone is invisible and not worth eating content over),
    and cap at 40px. Returns (left, top, right, bottom)."""
    lums = [np.asarray(img.convert("L")).astype(np.int16) for img in views]
    darks = [lum < 6 for lum in lums]
    center = darks[len(darks) // 2]
    w = center.shape[1]
    h = center.shape[0]
    l = t = r = b = 0
    cen_l, cen_r = edge_runs(center, axis=1)
    for a, lum in zip(darks, lums):
        rl, rr = edge_runs(a, axis=1)
        l = max(l, visible_margin(np.maximum(rl - cen_l, 0), rl,
                                  lambda y: lum[y, :], w, False))
        r = max(r, visible_margin(np.maximum(rr - cen_r, 0), rr,
                                  lambda y: lum[y, :], w, True))
    # top/bottom measured INSIDE the x-crop: the side wedges' mostly-black columns would
    # otherwise read as huge top runs and blow the vertical crop out to the cap
    xl = min(40, l + 2 if l else 0)
    xr = min(40, r + 2 if r else 0)
    cen_t, cen_b = edge_runs(center[:, xl:w - xr], axis=0)
    for a, lum in zip(darks, lums):
        rt, rb = edge_runs(a[:, xl:w - xr], axis=0)
        lc = lum[:, xl:w - xr]
        t = max(t, visible_margin(np.maximum(rt - cen_t, 0), rt,
                                  lambda x: lc[:, x], h, False))
        b = max(b, visible_margin(np.maximum(rb - cen_b, 0), rb,
                                  lambda x: lc[:, x], h, True))
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
