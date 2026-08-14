#!/usr/bin/env python3
"""Turn wallgen's PPM output into the distributable NyxOS wallpaper pack.

wallgen writes raw PPM because the kernel-side renderer has no business knowing
about zlib; this converts to PNG, drops each PPM as soon as it is converted (the
full set is ~1 GB of intermediates), and zips the result with a README.

    python pack.py WORKDIR OUTPUT.zip

WORKDIR is expected to already contain the .ppm files, laid out in the
subdirectories that become the folders inside the zip.
"""
import sys
import zipfile
from pathlib import Path

from PIL import Image


def convert(workdir: Path) -> list[Path]:
    """PPM -> PNG in place, deleting each source once it is safely converted."""
    pngs = []
    for ppm in sorted(workdir.rglob("*.ppm")):
        png = ppm.with_suffix(".png")
        with Image.open(ppm) as im:
            im.save(png, "PNG", optimize=True)
        ppm.unlink()
        pngs.append(png)
        print(f"  {png.relative_to(workdir)}  {png.stat().st_size // 1024} KB")
    return pngs


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    workdir, out = Path(sys.argv[1]), Path(sys.argv[2])

    print(f"converting {workdir} ...")
    pngs = convert(workdir)
    if not pngs:
        print("nothing to pack — did wallgen run?", file=sys.stderr)
        return 1

    readme = workdir / "README.txt"
    if not readme.exists():
        print("missing README.txt in workdir", file=sys.stderr)
        return 1

    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(readme, "README.txt")
        for png in pngs:
            z.write(png, str(png.relative_to(workdir)).replace("\\", "/"))

    print(f"\n{out}  —  {len(pngs)} wallpapers, {out.stat().st_size / 1e6:.1f} MB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
