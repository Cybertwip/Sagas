"""PNG helpers for Remix CSS portrait decode."""
import struct
import zlib


def rgba16(value):
    return (((value >> 11) & 31) * 255 // 31,
            ((value >> 6) & 31) * 255 // 31,
            ((value >> 1) & 31) * 255 // 31,
            255 if value & 1 else 0)


def scale2x(width, height, rgba):
    """Pixel-art upscale so CSS portraits are not drawn from raw 32x32."""
    out_w, out_h = width * 2, height * 2
    out = bytearray(out_w * out_h * 4)

    def pix(x, y):
        x = 0 if x < 0 else width - 1 if x >= width else x
        y = 0 if y < 0 else height - 1 if y >= height else y
        i = (y * width + x) * 4
        return rgba[i:i + 4]

    def put(x, y, p):
        i = (y * out_w + x) * 4
        out[i:i + 4] = p

    for y in range(height):
        for x in range(width):
            b, d, e, f, h = pix(x, y - 1), pix(x - 1, y), pix(x, y), pix(x + 1, y), pix(x, y + 1)
            if b != h and d != f:
                e0 = d if d == b else e
                e1 = f if b == f else e
                e2 = d if d == h else e
                e3 = f if h == f else e
            else:
                e0 = e1 = e2 = e3 = e
            put(x * 2, y * 2, e0)
            put(x * 2 + 1, y * 2, e1)
            put(x * 2, y * 2 + 1, e2)
            put(x * 2 + 1, y * 2 + 1, e3)
    return out_w, out_h, bytes(out)


def img_word_swap_16(width, height, raw):
    """Undo SP_TEXSHUF: odd rows swap adjacent 32-bit words of 16-bit texels."""
    row_bytes = width * 2
    even_width = row_bytes - 4 if ((row_bytes // 4) % 2) == 1 else row_bytes
    out = bytearray(raw)
    for y in range(1, height, 2):
        for x in range(0, even_width, 8):
            base = y * row_bytes + x
            out[base:base + 4], out[base + 4:base + 8] = out[base + 4:base + 8], out[base:base + 4]
    return bytes(out)


def decode_portrait(data, relocs, sprite_offset, width=32, height=32):
    image = relocs.get(sprite_offset - 8)
    if not image:
        return None
    offset = image[1]
    nbytes = width * height * 2
    if offset < 0 or offset + nbytes > len(data):
        return None
    raw = img_word_swap_16(width, height, data[offset:offset + nbytes])
    pixels = bytearray(width * height * 4)
    for index in range(width * height):
        value = struct.unpack_from(">H", raw, index * 2)[0]
        r, g, b, a = rgba16(value)
        pixels[index * 4:index * 4 + 4] = bytes((r, g, b, a))
    return width, height, bytes(pixels)


def write_png(path, width, height, rgba):
    def chunk(tag, payload):
        header = tag + payload
        return struct.pack(">I", len(payload)) + header + struct.pack(">I", zlib.crc32(header) & 0xffffffff)

    raw = b"".join(b"\x00" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(raw, 9)) +
        chunk(b"IEND", b""))
