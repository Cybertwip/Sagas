from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .selftest import run_selftest


def _default_paths() -> tuple[Path, Path]:
    tools = Path(__file__).resolve().parent.parent
    sagas = tools.parent
    return sagas / "build" / "assets", sagas / "assets" / "scenes"


def _dump_model(assets: Path, descriptor: str, layout: str, materials: str, animation: str) -> int:
    from .archive import RelocArchive
    from .model import GeometryLayout, ModelLoader
    from .pose import collect_textures, model_stats

    layouts = {
        "direct": GeometryLayout.Direct,
        "links": GeometryLayout.DisplayListLinks,
        "pairs": GeometryLayout.JointPairs,
    }
    archive = RelocArchive(assets)
    loader = ModelLoader(archive)
    if descriptor.endswith("DObjDesc") or "JointTree" in descriptor:
        if materials or animation:
            model = loader.model(descriptor, animation, layouts[layout], materials)
        else:
            try:
                model = loader.fighter_model(descriptor, layouts[layout])
            except Exception:
                model = loader.model(descriptor, animation, layouts[layout], materials)
    else:
        model = loader.display_list(descriptor, layouts[layout], materials)
    stats = model_stats(model)
    print(f"{descriptor}")
    for key, value in stats.items():
        print(f"  {key}: {value}")
    for texture in collect_textures(model):
        flag = ""
        if texture.missing_palette:
            flag += " MISSING_PALETTE"
        if texture.unmatched_format:
            flag += " UNMATCHED_FORMAT"
        print(f"  tex {texture.format_name:10} {texture.width:4}x{texture.height:<4} "
              f"{texture.source}{flag}")
    return 0


def main(argv: list[str] | None = None) -> int:
    assets_default, scene_default = _default_paths()
    parser = argparse.ArgumentParser(
        description="Play Sagas opening animation graphs and inspect N64 materials/textures.")
    parser.add_argument("--assets", type=Path, default=assets_default,
                        help="Unpacked Sagas asset root (sagas/build/assets)")
    parser.add_argument("--scene", type=Path, default=scene_default / "opening.scene.tsv")
    parser.add_argument("--sequence", type=Path, default=scene_default / "opening.sequence.tsv")
    parser.add_argument("--cues", type=Path, default=scene_default / "opening.cues.tsv")
    parser.add_argument("--self-test", action="store_true",
                        help="Decode opening models/materials without opening the Qt window")
    parser.add_argument("--dump-model", metavar="SYMBOL",
                        help="Print texture/material stats for one reloc symbol")
    parser.add_argument("--layout", choices=("direct", "links", "pairs"), default="direct")
    parser.add_argument("--materials", default="", help="MObjSub symbol for --dump-model")
    parser.add_argument("--animation", default="", help="AnimJoint symbol for --dump-model")
    args = parser.parse_args(argv)

    if not args.assets.exists():
        print(f"asset root does not exist: {args.assets}", file=sys.stderr)
        print("Build sagas first so sagas/build/assets is unpacked.", file=sys.stderr)
        return 2

    if args.self_test:
        failures = run_selftest(args.assets, args.scene.parent)
        if failures:
            print("self-test failed:")
            for failure in failures:
                print(f"  {failure}")
            return 1
        print("self-test passed")
        return 0

    if args.dump_model:
        return _dump_model(args.assets, args.dump_model, args.layout, args.materials, args.animation)

    try:
        from PySide6.QtGui import QSurfaceFormat
        from PySide6.QtWidgets import QApplication
        from .scene import SceneTable
        from .window import SequenceViewerWindow
    except ImportError as error:
        print("Qt viewer needs PySide6 and PyOpenGL:", error, file=sys.stderr)
        print("  python3 -m pip install -r sagas/tools/sequence_viewer/requirements.txt", file=sys.stderr)
        return 2

    fmt = QSurfaceFormat()
    fmt.setVersion(4, 1)
    fmt.setProfile(QSurfaceFormat.OpenGLContextProfile.CoreProfile)
    fmt.setDepthBufferSize(24)
    QSurfaceFormat.setDefaultFormat(fmt)
    app = QApplication(sys.argv)
    table = SceneTable(args.scene, args.sequence, args.cues)
    window = SequenceViewerWindow(args.assets, table)
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
