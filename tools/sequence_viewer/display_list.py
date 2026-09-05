from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from .archive import RelocArchive
from .texture import decode_image
from .types import Address, Color, Material, Mesh, RasterImage, Vertex


def _source_alpha_blend(mode: int) -> bool:
    cycle1 = ((mode >> 26) & 3) == 0 and ((mode >> 22) & 3) == 1 and ((mode >> 18) & 3) == 0
    cycle2 = ((mode >> 24) & 3) == 0 and ((mode >> 20) & 3) == 1 and ((mode >> 16) & 3) == 0
    return ((mode & 0x0C00) == 0x0800) or cycle1 or cycle2


def _texture_scale(scale: float, divisor: int) -> float:
    if abs(scale) < 1.0e-8:
        return 0.0
    return min((2097152.0 / divisor) / scale, 65535.0) / 65536.0


@dataclass
class _Image:
    address: Optional[Address] = None
    format: int = 0
    size: int = 0
    width: int = 1


@dataclass
class _Load:
    image: _Image
    uls: int = 0
    ult: int = 0
    lrs: int = 0
    lrt: int = 0
    size: int = 0
    block: bool = False


@dataclass
class _Tile:
    format: int = 0
    size: int = 0
    line: int = 0
    tmem: int = 0
    palette: int = 0
    cms: int = 0
    cmt: int = 0
    masks: int = 0
    maskt: int = 0
    shifts: int = 0
    shiftt: int = 0
    uls: int = 0
    ult: int = 0
    lrs: int = 0
    lrt: int = 0
    window_set: bool = False


@dataclass
class _Cached:
    vertex: Vertex = field(default_factory=Vertex)
    valid: bool = False


class DisplayListDecoder:
    """F3DEX2 interpreter matching sagas DisplayList.cpp, with Remix texel decode."""

    def __init__(self, archive: RelocArchive) -> None:
        self.archive = archive
        self.textures: dict[str, RasterImage] = {}

    def materials(self, table: Address, count: int) -> list[list[Material]]:
        """Decode an MObjSub pointer table (ssb-decomp-re src/sys/objtypes.h)."""
        result: list[list[Material]] = [[] for _ in range(count)]
        archive = self.archive
        for node in range(count):
            slot = table.shifted(node * 4)
            if archive.u32(slot) == 0:
                continue
            listing = archive.resolve(slot)
            if listing is None:
                continue
            for index in range(16):
                cell = listing.shifted(index * 4)
                if archive.u32(cell) == 0:
                    break
                sub = archive.resolve(cell)
                if sub is None:
                    break
                material = Material()
                sprites = archive.resolve(sub.shifted(4))
                material.sprites = sprites
                if sprites is not None and archive.u32(sprites) != 0:
                    material.image = archive.resolve(sprites)
                palettes = archive.resolve(sub.shifted(0x2C))
                material.palettes = palettes
                if palettes is not None and archive.u32(palettes) != 0:
                    material.palette = archive.resolve(palettes)
                material.format = archive.u8(sub.shifted(2))
                material.size = archive.u8(sub.shifted(3))
                material.width = max(archive.u16(sub.shifted(0x0C)), 1)
                material.height = max(archive.u16(sub.shifted(0x0E)), 1)
                flags = archive.u16(sub.shifted(0x30))
                material.flags = flags
                material.block_format = archive.u8(sub.shifted(0x32))
                material.block_size = archive.u8(sub.shifted(0x33))
                trau = archive.f32(sub.shifted(0x14))
                trav = archive.f32(sub.shifted(0x18))
                scau = archive.f32(sub.shifted(0x1C))
                scav = archive.f32(sub.shifted(0x20))
                divisor = max(archive.u16(sub.shifted(8)), 1)
                material.texture_scale_s = _texture_scale(scau, divisor)
                material.texture_scale_t = _texture_scale(scav, divisor)
                safe_s = scau if abs(scau) > 1.0e-8 else 1.0
                safe_t = scav if abs(scav) > 1.0e-8 else 1.0
                bias = archive.u16(sub.shifted(0x0A))
                material.tile_uls = max(int((((material.width * trau) + bias) / safe_s) * 4.0), 0)
                material.tile_ult = max(int(((((1.0 - scav) - trav) * material.height + bias) / safe_t) * 4.0), 0)
                material.tile_lrs = material.tile_uls + (material.width - 1) * 4
                material.tile_lrt = material.tile_ult + (material.height - 1) * 4
                material.primitive = Color(
                    archive.u8(sub.shifted(0x50)), archive.u8(sub.shifted(0x51)),
                    archive.u8(sub.shifted(0x52)), archive.u8(sub.shifted(0x53)))
                material.set_primitive = (flags & (0x0200 | 0x0010 | 0x0008)) != 0
                material.flags = flags
                if flags & 0x1000:
                    material.light1 = Color(
                        archive.u8(sub.shifted(0x60)), archive.u8(sub.shifted(0x61)),
                        archive.u8(sub.shifted(0x62)), archive.u8(sub.shifted(0x63)))
                if flags & 0x2000:
                    material.light2 = Color(
                        archive.u8(sub.shifted(0x64)), archive.u8(sub.shifted(0x65)),
                        archive.u8(sub.shifted(0x66)), archive.u8(sub.shifted(0x67)))
                result[node].append(material)
        return result

    def decode(self, display_list: Address, materials: Optional[list[Material]] = None) -> Mesh:
        mesh = Mesh()
        state = _State(materials or [])
        self._list(mesh, state, display_list, 0)
        return mesh

    def decode_links(self, links: Address, materials: Optional[list[Material]] = None) -> Mesh:
        result = Mesh()
        cursor = links
        for _ in range(64):
            list_id = self.archive.u32(cursor)
            if list_id == 4:
                return result
            address = self.archive.resolve(cursor.shifted(4))
            if address is not None:
                result.extend(self.decode(address, materials))
            cursor = cursor.shifted(8)
        raise RuntimeError("unterminated N64 display-list links")

    def decode_pairs(self, pairs: Address, materials: Optional[list[Material]] = None) -> Mesh:
        result = Mesh()
        for i in range(2):
            address = self.archive.resolve(pairs.shifted(i * 4))
            if address is not None:
                result.extend(self.decode(address, materials))
        return result

    def decode_joint_tree(
        self,
        pairs: list[Optional[Address]],
        materials: list[list[Material]],
    ) -> tuple[list[Mesh], list[Mesh]]:
        before = [Mesh() for _ in pairs]
        after = [Mesh() for _ in pairs]
        state = _State([])
        for node, pair in enumerate(pairs):
            state.materials = materials[node] if node < len(materials) else []
            state.material_index = None
            if pair is None:
                continue
            first = self.archive.resolve(pair)
            second = self.archive.resolve(pair.shifted(4))
            if first is not None:
                state.transform_node = node
                state.transform_parent = True
                self._list(before[node], state, first, 0)
            if second is not None:
                state.transform_node = node
                state.transform_parent = False
                self._list(after[node], state, second, 0)
        return before, after

    def decode_model_tree(
        self,
        display_lists: list[Optional[Address]],
        materials: list[list[Material]],
        linked: bool,
    ) -> list[Mesh]:
        result = [Mesh() for _ in display_lists]
        state = _State([])
        for node, display_list in enumerate(display_lists):
            state.materials = materials[node] if node < len(materials) else []
            state.material_index = None
            if display_list is None:
                continue
            if linked:
                cursor = display_list
                for _ in range(64):
                    if self.archive.u32(cursor) == 4:
                        break
                    target = self.archive.resolve(cursor.shifted(4))
                    if target is not None:
                        self._list(result[node], state, target, 0)
                    cursor = cursor.shifted(8)
                else:
                    raise RuntimeError("unterminated N64 display-list links")
            else:
                self._list(result[node], state, display_list, 0)
        return result

    def _apply_mobj(self, state: _State, material: Material) -> None:
        flags = material.flags
        if flags == 0:
            flags = 0x80 | 0x20 | 0x01
        if (flags & 0x4) and material.palette is not None:
            state.palette = material.palette
            state.image = _Image(material.palette, 0, 2, 1)
        if material.set_primitive:
            state.primitive = material.primitive
        if material.light1 is not None:
            state.light1 = material.light1
        if material.light2 is not None:
            state.light2 = material.light2
        if (flags & (0x01 | 0x02 | 0x10)) and material.image is not None:
            current = bool(flags & (0x01 | 0x10))
            fmt = material.format if current else material.block_format
            siz = material.size if current else material.block_size
            state.image = _Image(material.image, fmt, siz, material.width)
            state.texture_scale_s = material.texture_scale_s
            state.texture_scale_t = material.texture_scale_t
        if flags & 0x20:
            tile = state.tiles[state.render_tile]
            tile.uls = material.tile_uls
            tile.ult = material.tile_ult
            tile.lrs = material.tile_lrs
            tile.lrt = material.tile_lrt
            tile.window_set = True
        if flags & 0x80:
            state.texture_enabled = True

    def _list(self, mesh: Mesh, state: _State, address: Address, depth: int) -> None:
        if depth >= 32:
            raise RuntimeError("N64 display-list recursion limit")
        mesh.display_lists += 1
        archive = self.archive
        for _ in range(1_000_000):
            mesh.commands += 1
            w0 = archive.u32(address)
            w1 = archive.u32(address.shifted(4))
            opcode = w0 >> 24
            if opcode in (0x00, 0xE1, 0xE3, 0xE6, 0xE7, 0xE8, 0xE9, 0xF1):
                pass
            elif opcode == 0xFC:
                rgb = ((w0 >> 20) & 15, (w1 >> 28) & 15, (w0 >> 15) & 31, (w1 >> 15) & 7,
                       (w0 >> 5) & 15, (w1 >> 24) & 15, w0 & 31, (w1 >> 6) & 7)
                alpha = ((w0 >> 12) & 7, (w1 >> 12) & 7, (w0 >> 9) & 7, (w1 >> 9) & 7,
                         (w1 >> 21) & 7, (w1 >> 3) & 7, (w1 >> 18) & 7, w1 & 7)
                state.primitive_rgb = 3 in rgb
                state.primitive_alpha = 3 in alpha
            elif opcode in (0xF9, 0xFB):
                color = Color((w1 >> 24) & 255, (w1 >> 16) & 255, (w1 >> 8) & 255, w1 & 255)
                if opcode == 0xF9:
                    state.blend = color
                else:
                    state.environment = color
            elif opcode == 0xE2:
                if (w0 & 0xFFFF) == 0x001C:
                    state.render_mode = w1
                    state.translucent = _source_alpha_blend(state.render_mode)
            elif opcode == 0xB9:
                if (w0 & 0xFFFF) == 0x031D or (w0 & 0x00FFFFFF) == 0:
                    state.render_mode = w1
                    state.translucent = _source_alpha_blend(state.render_mode)
            elif opcode == 0x01:
                count = (w0 >> 12) & 0xFF
                end = (w0 >> 1) & 0x7F
                first = end - count if end >= count else 32
                source = archive.resolve(address.shifted(4))
                if source is None or count > 32 or first + count > 32:
                    raise RuntimeError("invalid N64 vertex load")
                for i in range(count):
                    vertex = source.shifted(i * 16)
                    out = state.cache[first + i]
                    packed = archive.u32(vertex.shifted(12))
                    lit = state.lighting
                    if lit:
                        def component(shift: int) -> float:
                            value = (packed >> shift) & 0xFF
                            if value >= 128:
                                value -= 256
                            return value / 127.0
                        color = state.primitive
                        nx, ny, nz = component(24), component(16), component(8)
                    else:
                        color = Color((packed >> 24) & 255, (packed >> 16) & 255,
                                      (packed >> 8) & 255, packed & 255)
                        nx = ny = nz = 0.0
                    out.vertex = Vertex(
                        x=float(archive.s16(vertex)),
                        y=float(archive.s16(vertex.shifted(2))),
                        z=float(archive.s16(vertex.shifted(4))),
                        u=archive.s16(vertex.shifted(8)) / 32.0,
                        v=archive.s16(vertex.shifted(10)) / 32.0,
                        nx=nx, ny=ny, nz=nz, lit=lit, color=color,
                        light1=state.light1, light2=state.light2,
                        transform_node=state.transform_node,
                        transform_parent=state.transform_parent,
                    )
                    out.valid = True
            elif opcode == 0x02:
                where = (w0 >> 16) & 0xFF
                vertex_index = (w0 & 0xFFFF) >> 1
                if vertex_index >= len(state.cache) or not state.cache[vertex_index].valid:
                    mesh.unsupported_commands += 1
                else:
                    vertex = state.cache[vertex_index].vertex
                    if where == 0x10:
                        if vertex.lit:
                            def component(shift: int) -> float:
                                value = (w1 >> shift) & 0xFF
                                return (value - 256 if value >= 128 else value) / 127.0
                            vertex.nx, vertex.ny, vertex.nz = (
                                component(24), component(16), component(8))
                        else:
                            vertex.color = Color((w1 >> 24) & 255, (w1 >> 16) & 255,
                                                 (w1 >> 8) & 255, w1 & 255)
                    elif where == 0x14:
                        s = (w1 >> 16) & 0xFFFF
                        t = w1 & 0xFFFF
                        vertex.u = (s - 0x10000 if s >= 0x8000 else s) / 32.0
                        vertex.v = (t - 0x10000 if t >= 0x8000 else t) / 32.0
            elif opcode == 0x05:
                self._triangle(mesh, state, (w0 >> 17) & 0x7F, (w0 >> 9) & 0x7F, (w0 >> 1) & 0x7F)
            elif opcode in (0x06, 0x07):
                self._triangle(mesh, state, (w0 >> 17) & 0x7F, (w0 >> 9) & 0x7F, (w0 >> 1) & 0x7F)
                self._triangle(mesh, state, (w1 >> 17) & 0x7F, (w1 >> 9) & 0x7F, (w1 >> 1) & 0x7F)
            elif opcode == 0xB1:
                self._triangle(mesh, state, ((w0 >> 16) & 0xFF) // 10, ((w0 >> 8) & 0xFF) // 10, (w0 & 0xFF) // 10)
                self._triangle(mesh, state, ((w1 >> 16) & 0xFF) // 10, ((w1 >> 8) & 0xFF) // 10, (w1 & 0xFF) // 10)
            elif opcode == 0xD9:
                state.geometry_mode = (state.geometry_mode & (w0 & 0x00FFFFFF)) | w1
                state.lighting = (state.geometry_mode & 0x00020000) != 0
            elif opcode == 0xB6:
                state.geometry_mode &= ~w1
                state.lighting = (state.geometry_mode & 0x00020000) != 0
            elif opcode == 0xB7:
                state.geometry_mode |= w1
                state.lighting = (state.geometry_mode & 0x00020000) != 0
            elif opcode == 0xD7:
                state.render_tile = (w0 >> 8) & 7
                state.texture_scale_s = ((w1 >> 16) & 0xFFFF) / 65536.0
                state.texture_scale_t = (w1 & 0xFFFF) / 65536.0
                state.texture_enabled = (w0 & 0xFF) != 0
            elif opcode == 0xDB:
                # F3DEX2 G_MOVEWORD / G_MW_LIGHTCOL, matching remix gfx_decode.c.
                if ((w0 >> 16) & 0xFF) == 0x0A:
                    color = Color((w1 >> 24) & 255, (w1 >> 16) & 255, (w1 >> 8) & 255, 255)
                    offset = w0 & 0xFFFF
                    if offset in (0, 4):
                        state.light1 = color
                    elif offset in (0x18, 0x1C):
                        state.light2 = color
            elif opcode == 0xDE:
                target = archive.resolve(address.shifted(4))
                if target is None:
                    if (w1 >> 24) == 0x0E:
                        material_index = (w1 & 0x00FFFFFF) // 8
                        if material_index < len(state.materials):
                            self._apply_mobj(state, state.materials[material_index])
                            state.material_index = material_index
                            mesh.material_commands += 1
                            address = address.shifted(8)
                            continue
                    mesh.unsupported_commands += 1
                    address = address.shifted(8)
                    continue
                self._list(mesh, state, target, depth + 1)
                if w0 & 0x00010000:
                    return
            elif opcode == 0xDF:
                return
            elif opcode == 0xFA:
                state.primitive = Color((w1 >> 24) & 255, (w1 >> 16) & 255, (w1 >> 8) & 255, w1 & 255)
            elif opcode == 0xF0:
                state.palette = state.image.address
            elif opcode == 0xF2:
                tile = state.tiles[(w1 >> 24) & 7]
                tile.uls = (w0 >> 12) & 0xFFF
                tile.ult = w0 & 0xFFF
                tile.lrs = (w1 >> 12) & 0xFFF
                tile.lrt = w1 & 0xFFF
                tile.window_set = True
            elif opcode == 0xF3:
                tile = state.tiles[(w1 >> 24) & 7]
                state.loads[tile.tmem] = _Load(state.image, (w0 >> 12) & 0xFFF, w0 & 0xFFF,
                                               (w1 >> 12) & 0xFFF, 0, tile.size, True)
            elif opcode == 0xF4:
                tile = state.tiles[(w1 >> 24) & 7]
                state.loads[tile.tmem] = _Load(state.image, (w0 >> 12) & 0xFFF, w0 & 0xFFF,
                                               (w1 >> 12) & 0xFFF, w1 & 0xFFF, tile.size, False)
            elif opcode == 0xF5:
                tile = state.tiles[(w1 >> 24) & 7]
                tile.format = (w0 >> 21) & 7
                tile.size = (w0 >> 19) & 3
                tile.line = (w0 >> 9) & 0x1FF
                tile.tmem = w0 & 0x1FF
                tile.palette = (w1 >> 20) & 15
                tile.cmt = (w1 >> 18) & 3
                tile.maskt = (w1 >> 14) & 15
                tile.shiftt = (w1 >> 10) & 15
                tile.cms = (w1 >> 8) & 3
                tile.masks = (w1 >> 4) & 15
                tile.shifts = w1 & 15
            elif opcode == 0xFD:
                state.image.format = (w0 >> 21) & 7
                state.image.size = (w0 >> 19) & 3
                state.image.width = (w0 & 0xFFF) + 1
                state.image.address = archive.resolve(address.shifted(4))
            else:
                mesh.unsupported_commands += 1
            address = address.shifted(8)
        raise RuntimeError("N64 display-list command budget exceeded")

    def _triangle(self, mesh: Mesh, state: _State, a: int, b: int, c: int) -> None:
        if a >= 32 or b >= 32 or c >= 32 or not (
                state.cache[a].valid and state.cache[b].valid and state.cache[c].valid):
            mesh.rejected_triangles += 1
            return
        image = self._texture(state)
        tile = state.tiles[state.render_tile]
        width = float(image.width) if image else 1.0
        height = float(image.height) if image else 1.0
        for index in (a, b, c):
            vertex = state.cache[index].vertex
            color = vertex.color
            light1, light2 = vertex.light1, vertex.light2
            if vertex.lit:
                rgb = state.primitive if state.primitive_rgb else Color()
                color = Color(rgb.r, rgb.g, rgb.b, state.primitive.a if state.primitive_alpha else 255)
                light1, light2 = state.light1, state.light2
            u = vertex.u * state.texture_scale_s - tile.uls * 0.25
            v = vertex.v * state.texture_scale_t - tile.ult * 0.25
            if tile.shifts:
                u *= (1.0 / float(1 << tile.shifts)) if tile.shifts <= 10 else float(1 << (16 - tile.shifts))
            if tile.shiftt:
                v *= (1.0 / float(1 << tile.shiftt)) if tile.shiftt <= 10 else float(1 << (16 - tile.shiftt))
            contact_shadow = (not vertex.lit and color.a < 255
                              and color.r < 8 and color.g < 8 and color.b < 8)
            if not vertex.lit and color.a == 0 and not contact_shadow:
                color = Color(color.r, color.g, color.b, 255)
            mesh.vertices.append(Vertex(
                x=vertex.x, y=vertex.y, z=vertex.z,
                u=u / width, v=v / height,
                nx=vertex.nx, ny=vertex.ny, nz=vertex.nz,
                lit=vertex.lit, color=color, light1=light1, light2=light2,
                texture=image,
                texture_mode_s=tile.cms, texture_mode_t=tile.cmt,
                texture_mask_s=tile.masks, texture_mask_t=tile.maskt,
                texture_window_s=((tile.lrs - tile.uls) >> 2) + 1 if tile.window_set and tile.lrs >= tile.uls
                else (image.width if image else 1),
                texture_window_t=((tile.lrt - tile.ult) >> 2) + 1 if tile.window_set and tile.lrt >= tile.ult
                else (image.height if image else 1),
                material_index=state.material_index if state.material_index is not None else 0xFFFF,
                transform_node=vertex.transform_node,
                transform_parent=vertex.transform_parent,
                translucent=state.translucent or contact_shadow,
            ))

    def _texture(self, state: _State) -> Optional[RasterImage]:
        if not state.texture_enabled:
            return None
        tile = state.tiles[state.render_tile]
        loaded = state.loads.get(tile.tmem)
        image = loaded.image if loaded is not None else state.image
        if image.address is None:
            return None
        palette_address = state.palette
        if tile.format == 2 and palette_address is None:
            # Smash Remix / HAL objdisplay load the DObj's MObj palette
            # (segment 0x0E) before CI sampling. Use that when the DL never
            # issued LoadTLUT — the usual Mario/Fox face-texture case.
            index = state.material_index if state.material_index is not None else 0
            candidates = []
            if index < len(state.materials):
                candidates.append(state.materials[index])
            candidates.extend(state.materials)
            for material in candidates:
                if material.palette is not None:
                    palette_address = material.palette
                    break
        bits = 4 << tile.size
        width = height = source_x = source_y = source_row_bytes = 0
        if loaded is not None and loaded.block:
            row_bytes = tile.line * 8
            load_bits = 4 << loaded.size
            load_units = (loaded.lrs - loaded.uls) + 1 if loaded.lrs >= loaded.uls else 0
            total_bits = load_units * load_bits
            if row_bytes != 0 and bits != 0:
                width = row_bytes * 8 // bits
                height = (total_bits + row_bytes * 8 - 1) // (row_bytes * 8)
                source_row_bytes = row_bytes
        elif loaded is not None:
            width = ((loaded.lrs - loaded.uls) >> 2) + 1 if loaded.lrs >= loaded.uls else 0
            height = ((loaded.lrt - loaded.ult) >> 2) + 1 if loaded.lrt >= loaded.ult else 0
            source_x = loaded.uls >> 2
            source_y = loaded.ult >> 2
            source_row_bytes = (image.width * bits + 7) // 8
        if width == 0 or height == 0:
            width = ((tile.lrs - tile.uls) >> 2) + 1 if tile.lrs >= tile.uls else image.width
            height = ((tile.lrt - tile.ult) >> 2) + 1 if tile.lrt >= tile.ult else 1
        if width == 0 or height == 0 or width > 1024 or height > 1024:
            return None
        if source_row_bytes == 0:
            source_row_bytes = (width * bits + 7) // 8
        palette_key = f"{palette_address.file}:{palette_address.offset}" if palette_address else ""
        key = (f"{image.address.file}:{image.address.offset}:{tile.format}:{tile.size}:"
               f"{width}:{height}:{source_x}:{source_y}:{source_row_bytes}:{tile.palette}:{palette_key}")
        cached = self.textures.get(key)
        if cached is not None:
            return cached
        source = self.archive.bytes_of(image.address.file)
        palette_bytes = self.archive.bytes_of(palette_address.file) if palette_address else None
        decoded = decode_image(
            source, tile.format, tile.size, width, height,
            source=image.address,
            palette=palette_bytes,
            palette_address=palette_address,
            palette_offset=palette_address.offset if palette_address else 0,
            palette_bank=tile.palette,
            source_x=source_x, source_y=source_y,
            source_row_bytes=source_row_bytes,
            image_base=image.address.offset,
            key=key,
        )
        self.textures[key] = decoded
        return decoded


@dataclass
class _State:
    materials: list[Material]
    cache: list[_Cached] = field(default_factory=lambda: [_Cached() for _ in range(32)])
    tiles: list[_Tile] = field(default_factory=lambda: [_Tile() for _ in range(8)])
    loads: dict[int, _Load] = field(default_factory=dict)
    image: _Image = field(default_factory=_Image)
    palette: Optional[Address] = None
    render_tile: int = 0
    texture_scale_s: float = 1.0
    texture_scale_t: float = 1.0
    geometry_mode: int = 0x00220405
    lighting: bool = True
    texture_enabled: bool = False
    primitive_rgb: bool = False
    primitive_alpha: bool = False
    blend: Color = field(default_factory=Color)
    environment: Color = field(default_factory=Color)
    primitive: Color = field(default_factory=Color)
    light1: Optional[Color] = None
    light2: Optional[Color] = None
    material_index: Optional[int] = None
    transform_node: int = 0xFFFF
    transform_parent: bool = False
    render_mode: int = 0
    translucent: bool = False
