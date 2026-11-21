"""Self-tests for png_probe.py: run anywhere (no GPU, no exe). They use synthetic PNGs
produced by an independent encoder, including every scanline filter type."""
import random
import struct
import unittest
import zlib

import png_probe as pp


def _chunk(ctype, body):
    return struct.pack(">I", len(body)) + ctype + body + struct.pack(">I", zlib.crc32(ctype + body) & 0xFFFFFFFF)


def encode_png(width, height, pixels, channels=4, filters=None, depth=8, interlace=0, color_type=None):
    """pixels: list of rows, each a bytes/bytearray of width*channels bytes."""
    color_type = color_type if color_type is not None else {3: 2, 4: 6}[channels]
    stride = width * channels
    out, prev = bytearray(), bytearray(stride)
    for y in range(height):
        f = (filters[y % len(filters)] if filters else 0)
        cur = bytearray(pixels[y])
        enc = bytearray(stride)
        for i in range(stride):
            a = cur[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 0:
                pred = 0
            elif f == 1:
                pred = a
            elif f == 2:
                pred = b
            elif f == 3:
                pred = (a + b) >> 1
            else:
                pred = pp._paeth(a, b, c)
            enc[i] = (cur[i] - pred) & 255
        out += bytes([f]) + enc
        prev = cur
    ihdr = struct.pack(">IIBBBBB", width, height, depth, color_type, 0, 0, interlace)
    return pp.PNG_SIGNATURE + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", zlib.compress(bytes(out))) + _chunk(b"IEND", b"")


def noise_image(w, h, channels, seed=1):
    rnd = random.Random(seed)
    return [bytes(rnd.randrange(256) for _ in range(w * channels)) for _ in range(h)]


def solid_rows(w, h, rgb, alpha=255):
    px = bytes(rgb) + bytes([alpha])
    return [px * w for _ in range(h)]


class DecoderTests(unittest.TestCase):
    def test_every_filter_type_round_trips_for_rgba_and_rgb(self):
        for channels in (4, 3):
            for f in range(5):
                with self.subTest(channels=channels, filter=f):
                    img = noise_image(37, 11, channels, seed=f + 10 * channels)
                    w, h, ch, rows = pp.decode_png(encode_png(37, 11, img, channels, filters=[f]))
                    self.assertEqual((w, h, ch), (37, 11, channels))
                    self.assertEqual([bytes(r) for r in rows], [bytes(r) for r in img])

    def test_mixed_filters_across_rows(self):
        img = noise_image(50, 20, 4, seed=7)
        _, _, _, rows = pp.decode_png(encode_png(50, 20, img, 4, filters=[0, 1, 2, 3, 4]))
        self.assertEqual([bytes(r) for r in rows], [bytes(r) for r in img])

    def test_rejects_malformed_and_unsupported_files(self):
        good = encode_png(4, 4, noise_image(4, 4, 4), 4)
        bad = {
            "empty": b"",
            "not png": b"GIF89a" + b"\0" * 40,
            "truncated": good[:len(good) // 2],
            "16-bit": encode_png(4, 4, noise_image(4, 4, 4), 4, depth=16),
            "interlaced": encode_png(4, 4, noise_image(4, 4, 4), 4, interlace=1),
            "palette": encode_png(4, 4, noise_image(4, 4, 4), 4, color_type=3),
            "grayscale": encode_png(4, 4, noise_image(4, 4, 4), 4, color_type=0),
        }
        for name, data in bad.items():
            with self.subTest(case=name):
                with self.assertRaises(pp.PngError):
                    pp.decode_png(data)

    def test_rejects_wrong_data_length_and_bad_filter(self):
        ihdr = struct.pack(">IIBBBBB", 4, 4, 8, 6, 0, 0, 0)
        short = pp.PNG_SIGNATURE + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", zlib.compress(b"\0" * 10)) + _chunk(b"IEND", b"")
        with self.assertRaises(pp.PngError):
            pp.decode_png(short)
        raw = b"".join(bytes([9]) + b"\0" * 16 for _ in range(4))   # filter type 9 does not exist
        badf = pp.PNG_SIGNATURE + _chunk(b"IHDR", ihdr) + _chunk(b"IDAT", zlib.compress(raw)) + _chunk(b"IEND", b"")
        with self.assertRaises(pp.PngError):
            pp.decode_png(badf)


def scene_like_image(object_rgb, w=200, h=120):
    """Background (31,33,38) with a large 'ground' band and a smaller 'object' block in object_rgb."""
    bg = (31, 33, 38)
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            in_ground = y > h * 0.55
            in_object = (60 < x < 120) and (25 < y < 80)
            rgb = object_rgb if (in_ground or in_object) else bg
            row += bytes(rgb) + b"\xff"
        rows.append(bytes(row))
    return w, h, rows


class AnalysisTests(unittest.TestCase):
    def analyze(self, rgb):
        w, h, rows = scene_like_image(rgb)
        return pp.analyze(*pp.decode_png(encode_png(w, h, rows, 4, filters=[1, 4])))

    def test_lit_surfaces_are_counted_as_lit_not_black(self):
        r = self.analyze((117, 149, 192))      # the engine's ground colour under the default light
        self.assertGreater(r["lit"], 0.4)
        self.assertLess(r["black"], 0.001)

    def test_the_unbound_constant_buffer_bug_is_detected(self):
        r = self.analyze((0, 0, 0))            # every surface black, background dark grey, like the first hardware run
        self.assertGreater(r["black"], 0.4)
        self.assertLess(r["lit"], 0.001)

    def test_dark_but_not_black_shading_is_neither_black_nor_lit(self):
        r = self.analyze((35, 45, 57))         # the darkest legitimate shade (ambient 0.25 only)
        self.assertLess(r["black"], 0.001)
        self.assertLess(r["lit"], 0.001)

    def test_background_alone_is_not_black(self):
        w, h, rows = 50, 40, solid_rows(50, 40, (31, 33, 38))
        r = pp.analyze(*pp.decode_png(encode_png(w, h, rows, 4)))
        self.assertEqual((r["black"], r["lit"]), (0.0, 0.0))


if __name__ == "__main__":
    unittest.main()
