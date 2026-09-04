from __future__ import annotations

from pathlib import Path
from typing import Optional

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QAction, QImage, QPixmap
from PySide6.QtWidgets import (
    QDockWidget, QHBoxLayout, QLabel, QListWidget, QListWidgetItem, QMainWindow,
    QMessageBox, QSlider, QSpinBox, QTableWidget, QTableWidgetItem, QTextEdit,
    QToolBar, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget,
)

from .archive import RelocArchive
from .glview import SequenceGLView
from .model import FIGHTER_LAYOUTS, GeometryLayout, Model3D, ModelLoader
from .pose import collect_textures, model_stats, pose_model
from .scene import SceneTable, fighter_from_portrait, opening_room_camera
from .types import FORMAT_NAMES, SIZE_NAMES, Color, Material, RasterImage


class SequenceViewerWindow(QMainWindow):
    def __init__(self, assets: Path, scene: SceneTable) -> None:
        super().__init__()
        self.assets = assets
        self.scene = scene
        self.archive = RelocArchive(assets)
        self.loader = ModelLoader(self.archive)
        self.models: dict[str, Model3D] = {}
        self.current_models: list[Model3D] = []
        self.frame = 0
        self.playing = False
        self.follow_scene_camera = True

        self.setWindowTitle("Sagas animation sequence viewer")
        self.resize(1440, 900)
        self.view = SequenceGLView()
        self.setCentralWidget(self.view)
        self._build_tree()
        self._build_inspector()
        self._build_timeline()
        self._build_toolbar()

        self.timer = QTimer(self)
        self.timer.setInterval(16)
        self.timer.timeout.connect(self._tick)
        self.timer.start()
        if self.scene.segments:
            self._select_segment(0)

    def _build_toolbar(self) -> None:
        bar = QToolBar("Debug")
        bar.setMovable(False)
        self.addToolBar(bar)
        self.play_action = QAction("Play", self, checkable=True)
        self.play_action.toggled.connect(self._set_playing)
        bar.addAction(self.play_action)
        for label, attr in (
            ("Textures", "use_textures"),
            ("Lighting", "use_lighting"),
            ("Wireframe", "wireframe"),
        ):
            action = QAction(label, self, checkable=True)
            action.setChecked(getattr(self.view, attr) if attr != "wireframe" else False)
            if attr == "use_textures":
                action.setChecked(True)
            if attr == "use_lighting":
                action.setChecked(True)
            action.toggled.connect(lambda on, name=attr: self._toggle(name, on))
            bar.addAction(action)
        self.follow_action = QAction("Scene camera", self, checkable=True)
        self.follow_action.setChecked(True)
        self.follow_action.toggled.connect(self._set_follow_camera)
        bar.addAction(self.follow_action)

    def _build_tree(self) -> None:
        dock = QDockWidget("Sequence", self)
        tree = QTreeWidget()
        tree.setHeaderLabels(["Name", "Kind"])
        segments = QTreeWidgetItem(["Opening timeline", "graph"])
        for segment in self.scene.segments:
            item = QTreeWidgetItem([
                f"{segment.name}  {segment.duration}t",
                segment.renderer,
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, ("segment", segment.name))
            for cue in segment.cues:
                child = QTreeWidgetItem([cue.resource, "cue"])
                child.setData(0, Qt.ItemDataRole.UserRole, ("resource", cue.resource))
                item.addChild(child)
            segments.addChild(item)
        resources = QTreeWidgetItem(["Resources", "bundles"])
        for bundle, entries in self.scene.bundles.items():
            parent = QTreeWidgetItem([bundle, "bundle"])
            parent.setData(0, Qt.ItemDataRole.UserRole, ("bundle", bundle))
            for resource in entries:
                child = QTreeWidgetItem([resource.key, resource.kind])
                child.setData(0, Qt.ItemDataRole.UserRole, ("resource", resource.key))
                parent.addChild(child)
            resources.addChild(parent)
        tree.addTopLevelItem(segments)
        tree.addTopLevelItem(resources)
        segments.setExpanded(True)
        resources.setExpanded(True)
        tree.itemSelectionChanged.connect(lambda: self._tree_selected(tree))
        dock.setWidget(tree)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, dock)
        self.tree = tree

    def _build_inspector(self) -> None:
        dock = QDockWidget("Materials", self)
        panel = QWidget()
        layout = QVBoxLayout(panel)
        self.stats = QLabel("No model loaded")
        self.stats.setWordWrap(True)
        layout.addWidget(self.stats)
        self.material_table = QTableWidget(0, 6)
        self.material_table.setHorizontalHeaderLabels(
            ["Node", "Mat", "Format", "Size", "Image", "Flags"])
        self.material_table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.material_table.itemSelectionChanged.connect(self._material_selected)
        layout.addWidget(self.material_table, 1)
        self.texture_list = QListWidget()
        self.texture_list.currentRowChanged.connect(self._texture_selected)
        layout.addWidget(self.texture_list)
        self.preview = QLabel("Texture preview")
        self.preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview.setMinimumHeight(160)
        self.preview.setStyleSheet("background:#111; color:#aaa;")
        layout.addWidget(self.preview)
        self.details = QTextEdit()
        self.details.setReadOnly(True)
        self.details.setMaximumHeight(180)
        layout.addWidget(self.details)
        dock.setWidget(panel)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, dock)

    def _build_timeline(self) -> None:
        dock = QDockWidget("Timeline", self)
        panel = QWidget()
        layout = QHBoxLayout(panel)
        self.frame_slider = QSlider(Qt.Orientation.Horizontal)
        self.frame_slider.setRange(0, max(self.scene.total_duration - 1, 0))
        self.frame_slider.valueChanged.connect(self._slider_changed)
        self.frame_spin = QSpinBox()
        self.frame_spin.setRange(0, max(self.scene.total_duration - 1, 0))
        self.frame_spin.valueChanged.connect(self._spin_changed)
        self.segment_label = QLabel("—")
        layout.addWidget(QLabel("Frame"))
        layout.addWidget(self.frame_slider, 1)
        layout.addWidget(self.frame_spin)
        layout.addWidget(self.segment_label)
        dock.setWidget(panel)
        self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, dock)

    def _toggle(self, name: str, on: bool) -> None:
        setattr(self.view, name, on)
        if name in ("use_textures", "wireframe", "use_lighting"):
            self.view.update()
        else:
            self._refresh()

    def _set_playing(self, on: bool) -> None:
        self.playing = on

    def _set_follow_camera(self, on: bool) -> None:
        self.follow_scene_camera = on
        self._refresh()

    def _tick(self) -> None:
        if not self.playing:
            return
        nxt = self.frame + 1
        if nxt >= self.scene.total_duration:
            nxt = 0
        self._set_frame(nxt)

    def _slider_changed(self, value: int) -> None:
        if value != self.frame:
            self._set_frame(value)

    def _spin_changed(self, value: int) -> None:
        if value != self.frame:
            self._set_frame(value)

    def _set_frame(self, frame: int) -> None:
        self.frame = max(0, min(frame, self.scene.total_duration - 1))
        self.frame_slider.blockSignals(True)
        self.frame_spin.blockSignals(True)
        self.frame_slider.setValue(self.frame)
        self.frame_spin.setValue(self.frame)
        self.frame_slider.blockSignals(False)
        self.frame_spin.blockSignals(False)
        position = self.scene.locate(self.frame)
        self.segment_label.setText(
            f"{position.segment.name}  local {position.local}/{position.segment.duration}  "
            f"{position.segment.renderer}")
        self._refresh()

    def _tree_selected(self, tree: QTreeWidget) -> None:
        items = tree.selectedItems()
        if not items:
            return
        kind, name = items[0].data(0, Qt.ItemDataRole.UserRole) or (None, None)
        try:
            if kind == "segment":
                index = next(i for i, segment in enumerate(self.scene.segments) if segment.name == name)
                self._select_segment(index)
            elif kind == "bundle":
                self._show_bundle(name)
            elif kind == "resource":
                self._show_resource(name)
        except Exception as error:
            QMessageBox.warning(self, "Load failed", str(error))

    def _select_segment(self, index: int) -> None:
        segment = self.scene.segments[index]
        start = sum(entry.duration for entry in self.scene.segments[:index])
        self.follow_scene_camera = True
        self.follow_action.setChecked(True)
        self._set_frame(start)
        if segment.bundle:
            self._show_bundle(segment.bundle, camera_from_scene=True)
        elif segment.renderer == "fighter":
            self._show_fighter_portrait(segment.argument)
        elif segment.renderer == "room":
            self._show_bundle("room.base", camera_from_scene=True)

    def _ensure_bundle(self, name: str) -> dict[str, Model3D]:
        for resource in self.scene.bundles[name]:
            if resource.dependency and resource.dependency not in self.models:
                dependency = self.scene.by_key[resource.dependency]
                if dependency.bundle != name:
                    self._ensure_bundle(dependency.bundle)
        missing = [resource.key for resource in self.scene.bundles[name] if resource.key not in self.models]
        if not missing:
            return {resource.key: self.models[resource.key] for resource in self.scene.bundles[name]}
        available = dict(self.models)
        built = self.scene.build_bundle(self.loader, name, available)
        self.models.update(built)
        return built

    def _show_bundle(self, name: str, camera_from_scene: bool = False) -> None:
        built = self._ensure_bundle(name)
        self.current_models = list(built.values())
        self._inspect(self.current_models[0] if self.current_models else None)
        self._refresh(frame_camera=not camera_from_scene)

    def _show_resource(self, key: str) -> None:
        resource = self.scene.by_key[key]
        if key not in self.models:
            available = dict(self.models)
            if resource.dependency and resource.dependency not in available:
                dep = self.scene.by_key[resource.dependency]
                self._ensure_bundle(dep.bundle)
                available = dict(self.models)
            self.models[key] = self.scene.build_resource(self.loader, resource, available)
        self.current_models = [self.models[key]]
        self.follow_scene_camera = False
        self.follow_action.setChecked(False)
        self._inspect(self.models[key])
        self._refresh(frame_camera=True)

    def _show_fighter_portrait(self, argument: str) -> None:
        spec = fighter_from_portrait(argument)
        if spec is None:
            self.current_models = []
            self._refresh()
            return
        descriptor, motion = spec
        cache_key = f"fighter:{descriptor}:{motion}"
        if cache_key not in self.models:
            model = self.loader.fighter_model(
                descriptor, FIGHTER_LAYOUTS.get(descriptor, GeometryLayout.Direct))
            if motion:
                self.loader.bind_fighter_animation(model, motion, model.fighter_wrapper)
            model.key = cache_key
            self.models[cache_key] = model
        self.current_models = [self.models[cache_key]]
        self.follow_scene_camera = False
        self.follow_action.setChecked(False)
        self._inspect(self.models[cache_key])
        self._refresh(frame_camera=True)

    def _inspect(self, model: Optional[Model3D]) -> None:
        self.material_table.setRowCount(0)
        self.texture_list.clear()
        self.preview.setPixmap(QPixmap())
        if model is None:
            self.stats.setText("No model loaded")
            self.details.setPlainText("")
            return
        stats = model_stats(model)
        self.stats.setText(
            f"{model.key or model.descriptor}\n"
            f"{stats['nodes']} joints, {stats['triangles']} triangles, "
            f"{stats['textures']} textures, {stats['materials']} MObjSubs, "
            f"{stats['material_commands']} material branches\n"
            f"textured {stats['textured_vertices']} / untextured {stats['untextured_vertices']} / "
            f"lit {stats['lit_vertices']}\n"
            f"missing palette {stats['missing_palette_vertices']}, "
            f"unmatched format {stats['unmatched_format_vertices']}")
        row = 0
        for node, materials in enumerate(model.materials):
            for index, material in enumerate(materials):
                self.material_table.insertRow(row)
                image = (f"{material.image.file}:{material.image.offset:#x}"
                         if material.image else "—")
                values = [
                    str(node), str(index),
                    FORMAT_NAMES.get(material.format, str(material.format)),
                    f"{material.width}x{material.height} {SIZE_NAMES.get(material.size, '')}",
                    image, f"{material.flags:#06x}",
                ]
                for column, value in enumerate(values):
                    item = QTableWidgetItem(value)
                    item.setData(Qt.ItemDataRole.UserRole, (node, index, material))
                    self.material_table.setItem(row, column, item)
                row += 1
        self.material_table.resizeColumnsToContents()
        for texture in collect_textures(model):
            item = QListWidgetItem(
                f"{texture.format_name} {texture.width}x{texture.height}  {texture.key}")
            item.setData(Qt.ItemDataRole.UserRole, texture)
            if texture.missing_palette:
                item.setForeground(Qt.GlobalColor.red)
            self.texture_list.addItem(item)
        lines = [
            f"descriptor {model.descriptor}",
            f"animation {model.animation_symbol or ('figatree' if model.fighter_animation else 'none')}",
            f"materials {model.material_symbol or ('file+0 MObjSub' if model.is_fighter else 'none')}",
            f"layout {model.layout.value}  lighting {'on' if model.receive_lighting else 'off'}",
        ]
        self.details.setPlainText("\n".join(lines))

    def _material_selected(self) -> None:
        items = self.material_table.selectedItems()
        if not items:
            self.view.isolate_node = None
            self.view.isolate_material = None
            self.view.set_batches(self.view.batches)
            return
        node, index, material = items[0].data(Qt.ItemDataRole.UserRole)
        self.view.isolate_node = node
        self.view.isolate_material = index
        self.view.set_batches(self.view.batches)
        self.details.setPlainText(self._material_text(node, index, material))

    def _material_text(self, node: int, index: int, material: Material) -> str:
        image = str(material.image) if material.image else "none"
        palette = str(material.palette) if material.palette else "none"
        return "\n".join((
            f"joint {node}  MObjSub {index}",
            f"fmt {FORMAT_NAMES.get(material.format, material.format)}  "
            f"siz {SIZE_NAMES.get(material.size, material.size)}  "
            f"{material.width}x{material.height}",
            f"image {image}",
            f"palette {palette}",
            f"scale s={material.texture_scale_s:.5f} t={material.texture_scale_t:.5f}",
            f"tile uls={material.tile_uls} ult={material.tile_ult} "
            f"lrs={material.tile_lrs} lrt={material.tile_lrt}",
            f"primitive {material.primitive.r},{material.primitive.g},"
            f"{material.primitive.b},{material.primitive.a}  set={material.set_primitive}",
            f"flags {material.flags:#06x}",
        ))

    def _texture_selected(self, row: int) -> None:
        item = self.texture_list.item(row)
        if item is None:
            return
        texture: RasterImage = item.data(Qt.ItemDataRole.UserRole)
        image = QImage(texture.rgba, texture.width, texture.height,
                       texture.width * 4, QImage.Format.Format_RGBA8888).copy()
        pixmap = QPixmap.fromImage(image)
        self.preview.setPixmap(pixmap.scaled(self.preview.size(), Qt.AspectRatioMode.KeepAspectRatio,
                                             Qt.TransformationMode.FastTransformation))
        self.details.setPlainText(
            f"{texture.format_name} {texture.width}x{texture.height}\n"
            f"source {texture.source}\n"
            f"palette {texture.palette or 'none'}\n"
            f"missing_palette={texture.missing_palette} unmatched={texture.unmatched_format}\n"
            f"key {texture.key}")

    def _refresh(self, frame_camera: bool = False) -> None:
        if not self.current_models:
            self.view.set_batches([])
            return
        position = self.scene.locate(self.frame)
        camera = None
        local = position.local
        if self.follow_scene_camera:
            if position.segment.renderer == "room":
                try:
                    camera = opening_room_camera(self.loader, local)
                except Exception:
                    camera = None
            elif position.segment.cues:
                try:
                    camera = self.loader.camera(position.segment.cues[0].camera, float(local))
                except Exception:
                    camera = None
        batches = []
        for model in self.current_models:
            tint = Color()
            if position.segment.renderer in ("models", "models_cockpit"):
                for cue in position.segment.cues:
                    if cue.resource == model.key:
                        tint = cue.tint
            batches.extend(pose_model(self.archive, model, float(local), tint))
        self.view.set_batches(
            batches,
            frame_camera=frame_camera and not (self.follow_scene_camera and camera),
            eye=camera.eye if camera else None,
            at=camera.at if camera else None,
            fov=camera.fov_y if camera else None,
        )
