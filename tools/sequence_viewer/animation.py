from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from .archive import RelocArchive
from .types import Address, Color, MATERIAL_COLORS, Node


@dataclass
class JointPose:
    tracks: list[float] = field(default_factory=lambda: [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0])
    flags: int = 0


@dataclass
class MaterialPose:
    tracks: list[float] = field(default_factory=lambda: [0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0])
    colors: list[Color] = field(default_factory=lambda: [
        Color(255, 255, 255, 255), Color(0, 0, 0, 255), Color(0, 0, 0, 0),
        Color(255, 255, 255, 255), Color(255, 255, 255, 255),
    ])


def pose_from_node(node: Node) -> JointPose:
    return JointPose(tracks=[
        node.rotate[0], node.rotate[1], node.rotate[2], 0.0,
        node.translate[0], node.translate[1], node.translate[2],
        node.scale[0], node.scale[1], node.scale[2],
    ])


def apply_pose(node: Node, pose: JointPose) -> Node:
    return Node(
        depth=node.depth,
        flags=node.flags,
        translate=(pose.tracks[4], pose.tracks[5], pose.tracks[6]),
        rotate=(pose.tracks[0], pose.tracks[1], pose.tracks[2]),
        scale=(pose.tracks[7], pose.tracks[8], pose.tracks[9]),
        display_list=node.display_list,
    )


def animation_table(archive: RelocArchive, address: Address, count: int) -> list[Optional[Address]]:
    scripts: list[Optional[Address]] = []
    for i in range(count):
        word = address.shifted(i * 4)
        scripts.append(None if archive.u32(word) == 0 else archive.resolve(word))
    return scripts


class _FloatTrack:
    __slots__ = ("kind", "base", "target", "rate_base", "rate_target", "start", "duration", "active")

    def __init__(self, value: float = 0.0) -> None:
        self.kind = "none"
        self.base = value
        self.target = value
        self.rate_base = 0.0
        self.rate_target = 0.0
        self.start = 0.0
        self.duration = 1.0
        self.active = False

    def value(self, time: float) -> float:
        if not self.active:
            return self.target
        length = min(max(time - self.start, 0.0), self.duration)
        if self.kind == "step":
            return self.target if self.duration == 0 or length >= self.duration else self.base
        if self.kind == "linear":
            return self.base + length * self.rate_base
        if self.kind != "cubic" or self.duration == 0:
            return self.target
        inv = 1.0 / self.duration
        x2 = length * length
        inv2 = inv * inv
        x3_inv2 = x2 * length * inv2
        twice = 2.0 * x3_inv2 * inv
        thrice = 3.0 * x2 * inv2
        x2_inv = x2 * inv
        tangent = x3_inv2 - x2_inv
        return (self.base * ((twice - thrice) + 1.0) + self.target * (thrice - twice)
                + self.rate_base * ((tangent - x2_inv) + length) + self.rate_target * tangent)


def _blend_color(a: Color, b: Color, amount: float) -> Color:
    def channel(src: int, dst: int) -> int:
        return int(round(src + (dst - src) * amount))
    return Color(channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b), channel(a.a, b.a))


class _ColorTrack:
    __slots__ = ("kind", "base", "target", "start", "duration", "active")

    def __init__(self, value: Color) -> None:
        self.kind = "none"
        self.base = value
        self.target = value
        self.start = 0.0
        self.duration = 1.0
        self.active = False

    def value(self, time: float) -> Color:
        if self.kind == "step":
            return self.target if self.duration == 0 or time - self.start >= self.duration else self.base
        amount = 1.0 if self.duration == 0 else min(max((time - self.start) / self.duration, 0.0), 1.0)
        return _blend_color(self.base, self.target, amount)


def _popcount(value: int) -> int:
    return bin(value).count("1")


def sample32(archive: RelocArchive, script: Address, frame: float, initial: JointPose) -> JointPose:
    """Evaluate an AObjEvent32 joint/camera script (ssb-decomp-re src/sys/objanim.c)."""
    tracks = [_FloatTrack(initial.tracks[i]) for i in range(10)]
    command = script
    cursor = 0.0
    budget = 16384
    pose = JointPose(list(initial.tracks), initial.flags)
    while budget > 0 and cursor <= frame:
        budget -= 1
        word = archive.u32(command)
        opcode = word >> 25
        flags = (word >> 15) & 0x3FF
        duration = float(word & 0x7FFF)
        command = command.shifted(4)
        if opcode == 0:
            break
        if opcode in (1, 14):
            target = archive.resolve(command)
            if target is None:
                break
            command = target
            continue
        if opcode == 2:
            cursor += duration
            continue
        if opcode == 13:
            command = command.shifted(4)
            continue
        if opcode == 15:
            pose.flags = flags
            cursor += duration
            continue
        if opcode == 16:
            cursor += duration
            continue
        if opcode == 17:
            command = command.shifted(_popcount(flags) * 4)
            cursor += duration
            continue
        if opcode == 7:
            for bit in range(10):
                if flags & (1 << bit):
                    tracks[bit].rate_target = archive.f32(command)
                    command = command.shifted(4)
            continue
        values = opcode in (3, 4, 5, 6, 8, 9, 10, 11)
        if not values:
            break
        for bit in range(10):
            if not (flags & (1 << bit)):
                continue
            track = tracks[bit]
            track.base = track.target
            track.target = archive.f32(command)
            command = command.shifted(4)
            track.start = cursor
            track.duration = duration
            track.active = True
            if opcode in (3, 4):
                track.kind = "linear"
                track.rate_base = (track.target - track.base) / duration if duration else 0.0
                track.rate_target = 0.0
            elif opcode in (5, 6):
                track.rate_base = track.rate_target
                track.rate_target = archive.f32(command)
                command = command.shifted(4)
                track.kind = "cubic"
            elif opcode in (8, 9):
                track.rate_base = track.rate_target
                track.rate_target = 0.0
                track.kind = "cubic"
            else:
                track.rate_target = 0.0
                track.kind = "step"
        if opcode in (3, 5, 8, 10):
            cursor += duration
    for i, track in enumerate(tracks):
        if track.active:
            pose.tracks[i] = track.value(frame)
    return pose


def sample_material(archive: RelocArchive, script: Address, frame: float,
                    initial: MaterialPose) -> MaterialPose:
    tracks = [_FloatTrack(initial.tracks[i]) for i in range(10)]
    colors = [_ColorTrack(initial.colors[i]) for i in range(len(MATERIAL_COLORS))]
    command = script
    cursor = 0.0
    budget = 16384
    pose = MaterialPose(list(initial.tracks), list(initial.colors))

    def packed_color(at: Address) -> Color:
        packed = archive.u32(at)
        return Color((packed >> 24) & 255, (packed >> 16) & 255, (packed >> 8) & 255, packed & 255)

    while budget > 0 and cursor <= frame:
        budget -= 1
        word = archive.u32(command)
        opcode = word >> 25
        flags = (word >> 15) & 0x3FF
        duration = float(word & 0x7FFF)
        command = command.shifted(4)
        if opcode == 0:
            break
        if opcode in (1, 14):
            target = archive.resolve(command)
            if target is None:
                break
            command = target
            continue
        if opcode == 2:
            cursor += duration
            continue
        if opcode == 12:
            continue
        if opcode == 13:
            command = command.shifted(4)
            continue
        if opcode in (15, 16):
            cursor += duration
            continue
        if opcode == 17:
            command = command.shifted(_popcount(flags) * 4)
            cursor += duration
            continue
        if 18 <= opcode <= 21:
            for bit in range(len(colors)):
                if flags & (1 << bit):
                    track = colors[bit]
                    track.base = track.target
                    track.target = packed_color(command)
                    command = command.shifted(4)
                    track.start = cursor
                    track.duration = duration
                    track.kind = "step" if opcode in (18, 19) else "linear"
                    track.active = True
            if opcode in (18, 20):
                cursor += duration
            continue
        if opcode == 22:
            command = command.shifted(_popcount(flags & 0x1F) * 4)
            cursor += duration
            continue
        if opcode == 7:
            for bit in range(10):
                if flags & (1 << bit):
                    tracks[bit].rate_target = archive.f32(command)
                    command = command.shifted(4)
            continue
        values = opcode in (3, 4, 5, 6, 8, 9, 10, 11)
        if not values:
            break
        for bit in range(10):
            if not (flags & (1 << bit)):
                continue
            track = tracks[bit]
            track.base = track.target
            track.target = archive.f32(command)
            command = command.shifted(4)
            track.start = cursor
            track.duration = duration
            track.active = True
            if opcode in (3, 4):
                track.kind = "linear"
                track.rate_base = (track.target - track.base) / duration if duration else 0.0
                track.rate_target = 0.0
            elif opcode in (5, 6):
                track.rate_base = track.rate_target
                track.rate_target = archive.f32(command)
                command = command.shifted(4)
                track.kind = "cubic"
            elif opcode in (8, 9):
                track.rate_base = track.rate_target
                track.rate_target = 0.0
                track.kind = "cubic"
            else:
                track.rate_target = 0.0
                track.kind = "step"
        if opcode in (3, 5, 8, 10):
            cursor += duration
    for i, track in enumerate(tracks):
        if track.active:
            pose.tracks[i] = track.value(frame)
    for i, track in enumerate(colors):
        if track.active:
            pose.colors[i] = track.value(frame)
    return pose


def _figatree_scale(raw: int, track: int, rate: bool) -> float:
    value_scale = (1.0 / 512.0, 1.0 / 4.0, 1.0 / 4096.0, 1.0 / 16384.0)
    rate_scale = (1.0 / 512.0, 1.0 / 32.0, 1.0 / 8192.0, 1.0 / 16384.0)
    kind = 0
    if 4 <= track <= 6:
        kind = 1
    elif track >= 7:
        kind = 2
    elif track == 3:
        kind = 3
    table = rate_scale if rate else value_scale
    return float(raw) * table[kind]


def sample16(archive: RelocArchive, script: Address, frame: float,
             initial: Optional[JointPose] = None) -> JointPose:
    """Evaluate a figatree AObjEvent16 fighter motion script."""
    if initial is None:
        initial = JointPose()
    tracks = [_FloatTrack(initial.tracks[i]) for i in range(10)]
    command = script
    cursor = 0.0
    budget = 65536
    pose = JointPose(list(initial.tracks), initial.flags)
    while budget > 0 and cursor <= frame:
        budget -= 1
        word = archive.u16(command)
        opcode = word >> 11
        flags = (word >> 1) & 0x3FF
        toggle = (word & 1) != 0
        command = command.shifted(2)
        if opcode == 0:
            break
        if opcode == 13:
            relative = archive.s16(command)
            command = Address(command.file, command.offset + relative)
            continue
        if opcode == 12:
            command = command.shifted(2)
            continue
        duration = 0.0
        if toggle:
            duration = float(archive.u16(command))
            command = command.shifted(2)
        if opcode == 1:
            cursor += duration
            continue
        if opcode == 11:
            continue
        if opcode == 14:
            pose.flags = flags
            cursor += duration
            continue
        if opcode == 6:
            for bit in range(10):
                if flags & (1 << bit):
                    tracks[bit].rate_target = _figatree_scale(archive.s16(command), bit, True)
                    command = command.shifted(2)
            continue
        if opcode < 2 or opcode > 10:
            break
        for bit in range(10):
            if not (flags & (1 << bit)):
                continue
            track = tracks[bit]
            track.base = track.target
            track.target = _figatree_scale(archive.s16(command), bit, False)
            command = command.shifted(2)
            track.start = cursor
            track.duration = duration
            track.active = True
            if opcode in (2, 3):
                track.kind = "linear"
                track.rate_base = (track.target - track.base) / duration if duration else 0.0
                track.rate_target = 0.0
            elif opcode in (4, 5):
                track.rate_base = track.rate_target
                track.rate_target = _figatree_scale(archive.s16(command), bit, True)
                command = command.shifted(2)
                track.kind = "cubic"
            elif opcode in (7, 8):
                track.rate_base = track.rate_target
                track.rate_target = 0.0
                track.kind = "cubic"
            else:
                track.rate_target = 0.0
                track.kind = "step"
        if opcode in (2, 4, 7, 9):
            cursor += duration
    for i, track in enumerate(tracks):
        if track.active:
            pose.tracks[i] = track.value(frame)
    return pose
