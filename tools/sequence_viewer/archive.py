from __future__ import annotations

import csv
import struct
from pathlib import Path
from typing import Optional

from .types import Address


class RelocArchive:
    """Repository over unpacked Sagas reloc files, matching sagas::n64::RelocArchive."""

    def __init__(self, assets: Path) -> None:
        self.assets = Path(assets)
        self._files: dict[int, bytes] = {}
        self._links: dict[tuple[int, int], Address] = {}
        self._links_loaded: set[int] = set()
        self.symbols: dict[str, Address] = {}
        self.file_names: dict[int, str] = {}
        self._load_symbols()
        self._load_manifest()

    def _load_symbols(self) -> None:
        path = self.assets / "reloc" / "symbols.tsv"
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream, delimiter="\t")
            for row in reader:
                self.symbols[row["symbol"]] = Address(int(row["file"]), int(row["offset"]))

    def _load_manifest(self) -> None:
        path = self.assets / "reloc" / "manifest.tsv"
        if not path.exists():
            return
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream, delimiter="\t")
            for row in reader:
                self.file_names[int(row["id"])] = row["name"]

    def symbol(self, name: str) -> Optional[Address]:
        if not name or name == "-":
            return None
        return self.symbols.get(name)

    def require_symbol(self, name: str) -> Address:
        found = self.symbol(name)
        if found is None:
            raise KeyError(f"missing reloc symbol: {name}")
        return found

    def bytes_of(self, file_id: int) -> bytes:
        blob = self._files.get(file_id)
        if blob is not None:
            return blob
        path = self.assets / "reloc" / f"{file_id:04d}.bin"
        blob = path.read_bytes()
        self._files[file_id] = blob
        return blob

    def _load_links(self, file_id: int) -> None:
        if file_id in self._links_loaded:
            return
        self._links_loaded.add(file_id)
        path = self.assets / "reloc" / f"{file_id:04d}.links.tsv"
        if not path.exists():
            return
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream, delimiter="\t")
            for row in reader:
                location = Address(file_id, int(row["location"]))
                self._links[(location.file, location.offset)] = Address(
                    int(row["target_file"]), int(row["target_offset"]))

    def resolve(self, pointer_word: Address) -> Optional[Address]:
        self._load_links(pointer_word.file)
        return self._links.get((pointer_word.file, pointer_word.offset))

    def u32(self, address: Address) -> int:
        data = self.bytes_of(address.file)
        end = address.offset + 4
        if end > len(data):
            raise IndexError(f"N64 u32 read past end of reloc {address}")
        return struct.unpack_from(">I", data, address.offset)[0]

    def s16(self, address: Address) -> int:
        data = self.bytes_of(address.file)
        if address.offset + 2 > len(data):
            raise IndexError(f"N64 s16 read past end of reloc {address}")
        return struct.unpack_from(">h", data, address.offset)[0]

    def u16(self, address: Address) -> int:
        data = self.bytes_of(address.file)
        if address.offset + 2 > len(data):
            raise IndexError(f"N64 u16 read past end of reloc {address}")
        return struct.unpack_from(">H", data, address.offset)[0]

    def f32(self, address: Address) -> float:
        return struct.unpack(">f", struct.pack(">I", self.u32(address)))[0]

    def u8(self, address: Address) -> int:
        data = self.bytes_of(address.file)
        if address.offset >= len(data):
            return 0
        return data[address.offset]

    def file_name(self, file_id: int) -> str:
        return self.file_names.get(file_id, f"reloc{file_id:04d}")
