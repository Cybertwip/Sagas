from __future__ import annotations

from .archive import RelocArchive
from .types import Address, Node

# DObjDesc terminator: index 18 (nGCCommonDObjEnd) in the low 12 bits.
_DOBJ_END = 18
_STRIDE = 44


def decode_skeleton(archive: RelocArchive, descriptor: Address, limit: int = 256) -> list[Node]:
    """Walk a DObjDesc chain. Layout matches ssb-decomp-re src/sys/objtypes.h."""
    nodes: list[Node] = []
    cursor = descriptor
    for _ in range(limit):
        ident = archive.u32(cursor)
        if (ident & 0xFFF) == _DOBJ_END:
            return nodes
        node = Node(
            depth=ident & 0xFFF,
            flags=ident & 0xF000,
            display_list=archive.resolve(cursor.shifted(4)),
            translate=(
                archive.f32(cursor.shifted(8)),
                archive.f32(cursor.shifted(12)),
                archive.f32(cursor.shifted(16)),
            ),
            rotate=(
                archive.f32(cursor.shifted(20)),
                archive.f32(cursor.shifted(24)),
                archive.f32(cursor.shifted(28)),
            ),
            scale=(
                archive.f32(cursor.shifted(32)),
                archive.f32(cursor.shifted(36)),
                archive.f32(cursor.shifted(40)),
            ),
        )
        nodes.append(node)
        cursor = cursor.shifted(_STRIDE)
    raise RuntimeError("unterminated N64 skeleton descriptor")
