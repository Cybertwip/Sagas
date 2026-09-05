from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional


@dataclass(frozen=True, slots=True)
class Address:
    file: int = 0
    offset: int = 0

    def at(self, offset: int) -> Address:
        return Address(self.file, offset)

    def shifted(self, delta: int) -> Address:
        return Address(self.file, self.offset + delta)

    def __str__(self) -> str:
        return f"{self.file}:{self.offset:#x}"


@dataclass(frozen=True, slots=True)
class Color:
    r: int = 255
    g: int = 255
    b: int = 255
    a: int = 255

    def modulate(self, other: Color) -> Color:
        return Color(self.r * other.r // 255, self.g * other.g // 255,
                     self.b * other.b // 255, self.a * other.a // 255)

    def tuple(self) -> tuple[float, float, float, float]:
        return (self.r / 255.0, self.g / 255.0, self.b / 255.0, self.a / 255.0)

    def rgb_tuple(self) -> tuple[float, float, float]:
        return (self.r / 255.0, self.g / 255.0, self.b / 255.0)


@dataclass(slots=True)
class Vec3:
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

    def as_tuple(self) -> tuple[float, float, float]:
        return (self.x, self.y, self.z)


FORMAT_NAMES = {0: "RGBA", 1: "YUV", 2: "CI", 3: "IA", 4: "I"}
SIZE_NAMES = {0: "4b", 1: "8b", 2: "16b", 3: "32b"}
WRAP_NAMES = {0: "wrap", 1: "mirror", 2: "clamp", 3: "mirror+clamp"}

JOINT_TRACKS = ("RotX", "RotY", "RotZ", "TraI", "TraX", "TraY", "TraZ", "ScaX", "ScaY", "ScaZ")
MATERIAL_TRACKS = (
    "TexId", "TraU", "TraV", "ScaU", "ScaV", "TexIdNext", "ScrU", "ScrV", "LFrac", "PaletteId",
)
MATERIAL_COLORS = ("Prim", "Env", "Blend", "Light1", "Light2")
CAMERA_TRACKS = ("EyeX", "EyeY", "EyeZ", "EyeI", "AtX", "AtY", "AtZ", "AtI", "UpX", "FovY")


@dataclass
class RasterImage:
    width: int
    height: int
    rgba: bytes
    key: str
    format: int = 0
    size: int = 0
    source: Optional[Address] = None
    palette: Optional[Address] = None
    missing_palette: bool = False
    unmatched_format: bool = False

    @property
    def format_name(self) -> str:
        fmt = FORMAT_NAMES.get(self.format, f"fmt{self.format}")
        siz = SIZE_NAMES.get(self.size, f"siz{self.size}")
        return f"{fmt}/{siz}"

    @property
    def pixel_count(self) -> int:
        return self.width * self.height


@dataclass
class Material:
    sprites: Optional[Address] = None
    palettes: Optional[Address] = None
    image: Optional[Address] = None
    palette: Optional[Address] = None
    format: int = 0
    size: int = 0
    width: int = 1
    height: int = 1
    block_format: int = 0
    block_size: int = 0
    flags: int = 0
    texture_scale_s: float = 1.0
    texture_scale_t: float = 1.0
    tile_uls: int = 0
    tile_ult: int = 0
    tile_lrs: int = 0
    tile_lrt: int = 0
    primitive: Color = field(default_factory=Color)
    set_primitive: bool = False
    light1: Optional[Color] = None
    light2: Optional[Color] = None


@dataclass
class Vertex:
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0
    u: float = 0.0
    v: float = 0.0
    nx: float = 0.0
    ny: float = 0.0
    nz: float = 0.0
    lit: bool = False
    color: Color = field(default_factory=Color)
    light1: Optional[Color] = None
    light2: Optional[Color] = None
    texture: Optional[RasterImage] = None
    texture_mode_s: int = 0
    texture_mode_t: int = 0
    texture_mask_s: int = 0
    texture_mask_t: int = 0
    texture_window_s: int = 1
    texture_window_t: int = 1
    material_index: int = 0xFFFF
    transform_node: int = 0xFFFF
    transform_parent: bool = False
    translucent: bool = False


@dataclass
class Mesh:
    vertices: list[Vertex] = field(default_factory=list)
    commands: int = 0
    display_lists: int = 0
    rejected_triangles: int = 0
    unsupported_commands: int = 0
    material_commands: int = 0

    def extend(self, other: Mesh) -> None:
        self.vertices.extend(other.vertices)
        self.commands += other.commands
        self.display_lists += other.display_lists
        self.rejected_triangles += other.rejected_triangles
        self.unsupported_commands += other.unsupported_commands
        self.material_commands += other.material_commands


@dataclass
class Node:
    depth: int = 0
    flags: int = 0
    translate: tuple[float, float, float] = (0.0, 0.0, 0.0)
    rotate: tuple[float, float, float] = (0.0, 0.0, 0.0)
    scale: tuple[float, float, float] = (1.0, 1.0, 1.0)
    display_list: Optional[Address] = None
