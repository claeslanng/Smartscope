#!/usr/bin/env python3
"""Turn RenderTest's screenshot renders into the README and website images.

    RenderTest --site renders               # writes renders/site_*.png and renders/drag_frames/
    python3 Tests/make_site_images.py renders --readme docs/images \
        --site ../claeslanng.github.io/assets/img/smartscope

Needs Pillow (pip install pillow) and, for the demo video, ffmpeg.

README (--readme), lossless PNG:
    main.png      the top image (2280 x 784, Graphite, half a bar)
    zoom.png      the first ~14 ms of a kick at Zoom x68
    themes.png    the four themes in a 2 x 2 grid
    settings.png  the settings card (2x)

Website (--site), as the product page expects:
    main.webp, zoom.webp            lossless, 2280 x 784
    main-share.jpg                  1200 px wide, for social previews (some sites don't read WebP)
    theme-<name>.webp               lossless, 1140 px wide for the two-column grid
    settings.webp                   lossless, 2x (show it at half its pixel size)
    drag-loop.mp4                   muted H.264 loop, 1280 x 440 (the hand dragging the view)
    drag-loop-poster.webp           its first frame
    resize-loop.mp4                 muted H.264 loop, 1600 x 702 (resizing, scope only), on the
                                    page's background so it shows no box
    resize-loop-poster.webp         its first frame
    fullscreen.webp                 lossless, 1920 x 920 (rendered at 1x, like a big monitor)
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

THEMES = ["graphite", "midnight", "phosphor", "daylight"]
GRID_GAP = 10
GRID_BACKGROUND = (13, 17, 23)
VIDEO_FPS = 30
# (frames folder, frame name prefix, output name, size)
VIDEOS = [
    ("drag_frames", "site_drag", "drag-loop", (1280, 440)),
    ("resize_frames", "site_resize", "resize-loop", (1600, 702)),
]


def load(renders: Path, name: str) -> Image.Image:
    path = renders / f"{name}.png"
    if not path.exists():
        sys.exit(f"Missing {path}. Run: RenderTest --site {renders}")
    return Image.open(path).convert("RGB")


def theme_tile(renders: Path, name: str) -> Image.Image:
    img = load(renders, f"site_theme_{name}")
    return img.resize((img.width // 2, img.height // 2), Image.LANCZOS)


def write_readme(renders: Path, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    load(renders, "site_main").save(out / "main.png", optimize=True)
    load(renders, "site_zoom").save(out / "zoom.png", optimize=True)
    load(renders, "site_settings").save(out / "settings.png", optimize=True)

    tiles = [theme_tile(renders, t) for t in THEMES]
    w, h = tiles[0].size
    grid = Image.new("RGB", (2 * w + GRID_GAP, 2 * h + GRID_GAP), GRID_BACKGROUND)
    for i, tile in enumerate(tiles):
        grid.paste(tile, ((i % 2) * (w + GRID_GAP), (i // 2) * (h + GRID_GAP)))
    grid.save(out / "themes.png", optimize=True)
    print(f"README images written to {out}")


def write_video(frames_dir: Path, prefix: str, out_stem: Path, size) -> None:
    frames = sorted(frames_dir.glob(f"{prefix}_*.png"))
    if not frames:
        print(f"No frames in {frames_dir}: skipped {out_stem.name}")
        return
    # Lossy poster: it loads with the page before anything plays (lossless was ~3x bigger)
    Image.open(frames[0]).convert("RGB").resize(size, Image.LANCZOS).save(
        out_stem.with_name(out_stem.name + "-poster.webp"), quality=88, method=6, use_sharp_yuv=True)
    if not shutil.which("ffmpeg"):
        print(f"ffmpeg not found: skipped {out_stem.name}.mp4")
        return
    subprocess.run([
        "ffmpeg", "-y", "-loglevel", "error",
        "-framerate", str(VIDEO_FPS), "-i", str(frames_dir / f"{prefix}_%04d.png"),
        # BT.709 conversion, tagged as such, so browsers show the exact colors (untagged
        # video drifted 1-2 levels, enough to show a faint box around the resize loop)
        "-vf", f"scale={size[0]}:{size[1]}:flags=lanczos:out_color_matrix=bt709:out_range=tv,format=yuv420p",
        "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "20",
        "-movflags", "+faststart", "-an", str(out_stem.with_name(out_stem.name + ".mp4")),
    ], check=True)


def write_site(renders: Path, out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    main = load(renders, "site_main")
    main.save(out / "main.webp", lossless=True, method=6)
    share = main.resize((1200, round(main.height * 1200 / main.width)), Image.LANCZOS)
    share.save(out / "main-share.jpg", quality=90, optimize=True, progressive=True)
    load(renders, "site_zoom").save(out / "zoom.webp", lossless=True, method=6)
    load(renders, "site_settings").save(out / "settings.webp", lossless=True, method=6)
    load(renders, "site_fullscreen").save(out / "fullscreen.webp", lossless=True, method=6)
    for t in THEMES:
        theme_tile(renders, t).save(out / f"theme-{t}.webp", lossless=True, method=6)

    for folder, prefix, name, size in VIDEOS:
        write_video(renders / folder, prefix, out / name, size)
    print(f"Website images written to {out}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("renders", type=Path, help="the folder RenderTest --site wrote to")
    parser.add_argument("--readme", type=Path, help="write the README images here (docs/images)")
    parser.add_argument("--site", type=Path, help="write the website images here (assets/img/smartscope)")
    args = parser.parse_args()
    if not args.readme and not args.site:
        parser.error("give --readme and/or --site")
    if args.readme:
        write_readme(args.renders, args.readme)
    if args.site:
        write_site(args.renders, args.site)


if __name__ == "__main__":
    main()
