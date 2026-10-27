"""Empaqueta una release de KoniSynth: dist/KoniSynth-<versión>-windows-x64.zip y dist/KoniSynth-<versión>-linux-x86_64.zip.

Contenido de cada zip: los tres formatos (VST3, CLAP, standalone), README.md, LICENSE, THIRD-PARTY-NOTICES.txt, docs/ y el
puente del agente (KoniSynth-mcp, sin .venv ni cachés).

Uso:  python tools/package_release.py [--windows <carpeta Release>] [--linux <carpeta Release>]
      Por defecto: build/KoniSynth_artefacts/Release (Windows) y build-linux/KoniSynth_artefacts/Release (Linux), si existen.
La versión se lee de KONI_VERSION_LABEL en CMakeLists.txt. En el zip de Linux, el ejecutable y el .so llevan permisos 755.
"""
from __future__ import annotations

import argparse
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
COMMON = ["README.md", "LICENSE", "THIRD-PARTY-NOTICES.txt"]
BRIDGE = ROOT / "Agent" / "KoniSynth-mcp"
BRIDGE_FILES = ["konisynth_mcp.py", "analysis.py", "generator.py", "requirements.txt", "README.md"]


def version() -> str:
    m = re.search(r'KONI_VERSION_LABEL="([^"]+)"', (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not m:
        raise SystemExit("KONI_VERSION_LABEL not found in CMakeLists.txt")
    return m.group(1)


def add(z: zipfile.ZipFile, src: Path, arc: str, executable: bool = False) -> None:
    info = zipfile.ZipInfo.from_file(src, arc)
    info.compress_type = zipfile.ZIP_DEFLATED
    mode = 0o755 if executable else 0o644
    info.external_attr = (0o100000 | mode) << 16  # fichero regular + permisos Unix
    z.writestr(info, src.read_bytes())


SKIP_SUFFIXES = (".iobj", ".ipdb", ".pdb", ".ilk", ".exp", ".lib")  # intermedios de MSVC (la enlazada incremental los deja en el bundle)


def add_tree(z: zipfile.ZipFile, src: Path, arc: str, exec_suffixes: tuple[str, ...] = ()) -> None:
    for f in sorted(src.rglob("*")):
        if f.is_file() and f.suffix.lower() not in SKIP_SUFFIXES:
            add(z, f, f"{arc}/{f.relative_to(src).as_posix()}", f.suffix in exec_suffixes)


def package(platform: str, release: Path, name: str) -> Path:
    vst3, clap = release / "VST3" / "KoniSynth.vst3", release / "CLAP" / "KoniSynth.clap"
    exe = release / "Standalone" / ("KoniSynth.exe" if platform == "windows" else "KoniSynth")
    for p in (vst3, clap, exe):
        if not p.exists():
            raise SystemExit(f"missing: {p}")
    DIST.mkdir(exist_ok=True)
    out = DIST / f"{name}.zip"
    with zipfile.ZipFile(out, "w") as z:
        add_tree(z, vst3, f"{name}/KoniSynth.vst3", exec_suffixes=(".so",))
        add(z, clap, f"{name}/KoniSynth.clap", executable=platform == "linux")
        add(z, exe, f"{name}/{exe.name}", executable=platform == "linux")
        for f in COMMON:
            add(z, ROOT / f, f"{name}/{f}")
        add_tree(z, ROOT / "docs", f"{name}/docs")
        for f in BRIDGE_FILES:
            add(z, BRIDGE / f, f"{name}/KoniSynth-mcp/{f}")
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--windows", type=Path, default=ROOT / "build" / "KoniSynth_artefacts" / "Release")
    ap.add_argument("--linux", type=Path, default=ROOT / "build-linux" / "KoniSynth_artefacts" / "Release")
    a = ap.parse_args()
    v = version()
    for platform, release, suffix in (("windows", a.windows, "windows-x64"), ("linux", a.linux, "linux-x86_64")):
        if release.exists():
            out = package(platform, release, f"KoniSynth-{v}-{suffix}")
            print(f"{out}  {out.stat().st_size / 1e6:.1f} MB")
        else:
            print(f"skipped {platform}: {release} not found")


if __name__ == "__main__":
    main()
