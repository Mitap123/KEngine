"""Minimal PNG reader and screenshot analysis for the viewport visual probe.

Pure standard library (zlib + struct). Supports 8-bit, non-interlaced RGB and
RGBA PNGs with all five scanline filters, which is what Qt's PNG writer
produces. Anything else is rejected with a clear error instead of being
misread. Used by test_viewport_screenshot.py; verified by test_png_probe.py.
"""
import struct
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(Exception):
    pass


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode_png(data: bytes):
    """Returns (width, height, channels, rows) where rows is a list of bytearrays (unfiltered)."""
    if not data.startswith(PNG_SIGNATURE):
        raise PngError("not a PNG file (bad signature)")
    pos = len(PNG_SIGNATURE)
    ihdr = None
    idat = bytearray()
    while pos + 8 <= len(data):
        length, ctype = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if len(body) != length:
            raise PngError("truncated chunk %r" % ctype)
        pos += 12 + length  # length + type + body + crc
        if ctype == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break
    if ihdr is None:
        raise PngError("missing IHDR")
    width, height, depth, color_type, _compression, _filter, interlace = ihdr
    if depth != 8:
        raise PngError("unsupported bit depth %d (only 8)" % depth)
    if interlace != 0:
        raise PngError("interlaced PNGs are not supported")
    channels = {2: 3, 6: 4}.get(color_type)
    if channels is None:
        raise PngError("unsupported color type %d (only RGB and RGBA)" % color_type)
    if width <= 0 or height <= 0 or width * height > 64_000_000:
        raise PngError("implausible image size %dx%d" % (width, height))
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error as e:
        raise PngError("corrupt image data: %s" % e)
    stride = width * channels
    if len(raw) != height * (stride + 1):
        raise PngError("image data has the wrong length")

    rows, prev = [], bytearray(stride)
    for y in range(height):
        off = y * (stride + 1)
        ftype = raw[off]
        line = bytearray(raw[off + 1:off + 1 + stride])
        if ftype == 0:
            pass
        elif ftype == 1:  # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif ftype == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ftype == 3:  # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif ftype == 4:  # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                c = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(a, prev[i], c)) & 255
        else:
            raise PngError("bad filter type %d" % ftype)
        rows.append(line)
        prev = line
    return width, height, channels, rows


def analyze(width, height, channels, rows):
    """Counts pixel classes used by the viewport probe.

    black:  r,g,b all <= 5   (what an unlit / unbound-constant-buffer surface renders as)
    lit:    max channel > 90 and blue exceeds red by > 20 (the engine's light-blue surfaces when lit)
    Returns fractions of all pixels.
    """
    total = width * height
    black = lit = 0
    for line in rows:
        for x in range(0, width * channels, channels):
            r, g, b = line[x], line[x + 1], line[x + 2]
            if r <= 5 and g <= 5 and b <= 5:
                black += 1
            elif max(r, g, b) > 90 and b > r + 20:
                lit += 1
    return {"width": width, "height": height, "black": black / total, "lit": lit / total}
