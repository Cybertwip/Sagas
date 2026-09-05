"""N64 texel decode, including intensity in both the RGB and alpha channels."""

from __future__ import annotations

from typing import Optional

from .types import Address, Color, RasterImage


def expand_4(value: int) -> int:
    value &= 0x0F
    return (value << 4) | value


def expand_5(value: int) -> int:
    value &= 0x1F
    return (value << 3) | (value >> 2)


def rgba5551(value: int) -> Color:
    return Color(expand_5(value >> 11), expand_5(value >> 6), expand_5(value >> 1),
                 255 if value & 1 else 0)


def nibble(data: bytes, pixel: int) -> int:
    if pixel // 2 >= len(data):
        return 0
    byte = data[pixel // 2]
    return byte >> 4 if (pixel & 1) == 0 else byte & 0x0F


def decode_texel(
    data: bytes,
    pixel: int,
    fmt: int,
    siz: int,
    palette: Optional[bytes] = None,
    palette_offset: int = 0,
    palette_bank: int = 0,
) -> tuple[Color, bool, bool]:
    """Return (color, missing_palette, unmatched_format)."""
    missing = False
    unmatched = False
    if fmt == 0 and siz == 2:
        at = pixel * 2
        if at + 1 >= len(data):
            return Color(255, 0, 255, 255), False, True
        value = (data[at] << 8) | data[at + 1]
        color = rgba5551(value)
    elif fmt == 0 and siz == 3:
        at = pixel * 4
        if at + 3 >= len(data):
            return Color(255, 0, 255, 255), False, True
        color = Color(data[at], data[at + 1], data[at + 2], data[at + 3])
    elif fmt == 2:
        if siz == 0:
            index = nibble(data, pixel) + palette_bank * 16
        else:
            index = data[pixel] if pixel < len(data) else 0
        if palette is not None:
            at = palette_offset + index * 2
            if at + 1 < len(palette):
                color = rgba5551((palette[at] << 8) | palette[at + 1])
            else:
                color = Color(0, 0, 0, 0)
        else:
            missing = True
            level = index * 17 if siz == 0 else index
            color = Color(level & 255, level & 255, level & 255, 255)
    elif fmt == 3:
        if siz == 0:
            value = nibble(data, pixel)
            intensity = ((value >> 1) & 7) * 255 // 7
            alpha = 255 if value & 1 else 0
        elif siz == 1:
            value = data[pixel] if pixel < len(data) else 0
            intensity = expand_4(value >> 4)
            alpha = expand_4(value & 0x0F)
        else:
            at = pixel * 2
            intensity = data[at] if at < len(data) else 0
            alpha = data[at + 1] if at + 1 < len(data) else 0
        color = Color(intensity, intensity, intensity, alpha)
    elif fmt == 4:
        if siz == 0:
            intensity = nibble(data, pixel) * 17
        else:
            intensity = data[pixel] if pixel < len(data) else 0
        color = Color(intensity, intensity, intensity, intensity)
    else:
        unmatched = True
        color = Color(255, 0, 255, 255)
    return color, missing, unmatched


def decode_image(
    data: bytes,
    fmt: int,
    siz: int,
    width: int,
    height: int,
    *,
    source: Optional[Address] = None,
    palette: Optional[bytes] = None,
    palette_address: Optional[Address] = None,
    palette_offset: int = 0,
    palette_bank: int = 0,
    source_x: int = 0,
    source_y: int = 0,
    source_row_bytes: int = 0,
    image_base: int = 0,
    key: str = "",
) -> RasterImage:
    bits = 4 << siz
    if source_row_bytes == 0:
        source_row_bytes = (width * bits + 7) // 8
    rgba = bytearray(width * height * 4)
    missing = False
    unmatched = False
    for y in range(height):
        row = image_base + (source_y + y) * source_row_bytes
        for x in range(width):
            source_pixel = source_x + x
            if fmt == 0 and siz == 2:
                at = row + source_pixel * 2
                texel = data[at:at + 2]
                color, miss, bad = decode_texel(texel, 0, fmt, siz)
            elif fmt == 0 and siz == 3:
                at = row + source_pixel * 4
                texel = data[at:at + 4]
                color, miss, bad = decode_texel(texel, 0, fmt, siz)
            elif fmt == 2 and siz == 0:
                at = row + source_pixel // 2
                texel = data[at:at + 1] if at < len(data) else b"\x00"
                # Reconstruct nibble addressing relative to source_pixel parity.
                color, miss, bad = decode_texel(
                    data[row:], source_pixel, fmt, siz, palette, palette_offset, palette_bank)
            elif fmt == 2:
                at = row + source_pixel
                texel = data[at:at + 1]
                color, miss, bad = decode_texel(
                    texel, 0, fmt, siz, palette, palette_offset, palette_bank)
            elif fmt == 3 and siz == 0:
                color, miss, bad = decode_texel(
                    data[row:], source_pixel, fmt, siz)
            elif fmt == 3 and siz == 2:
                at = row + source_pixel * 2
                texel = data[at:at + 2]
                color, miss, bad = decode_texel(texel, 0, fmt, siz)
            elif fmt == 3:
                at = row + source_pixel
                texel = data[at:at + 1]
                color, miss, bad = decode_texel(texel, 0, fmt, siz)
            elif fmt == 4 and siz == 0:
                color, miss, bad = decode_texel(data[row:], source_pixel, fmt, siz)
            elif fmt == 4:
                at = row + source_pixel
                texel = data[at:at + 1]
                color, miss, bad = decode_texel(texel, 0, fmt, siz)
            else:
                color, miss, bad = Color(255, 0, 255, 255), False, True
            missing = missing or miss
            unmatched = unmatched or bad
            out = (y * width + x) * 4
            rgba[out] = color.r
            rgba[out + 1] = color.g
            rgba[out + 2] = color.b
            rgba[out + 3] = color.a
    return RasterImage(
        width=width,
        height=height,
        rgba=bytes(rgba),
        key=key,
        format=fmt,
        size=siz,
        source=source,
        palette=palette_address,
        missing_palette=missing,
        unmatched_format=unmatched,
    )
