"""Visual probe for the Direct3D viewport spike (Windows). Runs the real exe and
inspects the PNG it writes. Needs a GPU or WARP; skipped (and reported as skipped)
unless KENGINE_VIEWPORT_SPIKE points at k_viewport_spike.exe.

Qt's DLLs must be findable (PATH or windeployqt), as for running the exe by hand.
Set KENGINE_VIEWPORT_NO_WARP=1 to insist on the real GPU (fails instead of using WARP).
KENGINE_VIEWPORT_ARGS adds arguments before --screenshot (k_editor needs --demo to have content).
"""
import os
import shlex
import subprocess
import tempfile
import unittest

import png_probe as pp

EXE = os.environ.get("KENGINE_VIEWPORT_SPIKE", "")


@unittest.skipUnless(EXE, "KENGINE_VIEWPORT_SPIKE is not set (viewport spike not built)")
class ViewportScreenshotTests(unittest.TestCase):
    def test_screenshot_is_lit_and_not_black(self):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "shot.png")
            extra = shlex.split(os.environ.get("KENGINE_VIEWPORT_ARGS", ""))     # e.g. "--demo" for k_editor
            args = [EXE] + extra + ["--screenshot", out] + (["--no-warp"] if os.environ.get("KENGINE_VIEWPORT_NO_WARP") else [])
            r = subprocess.run(args, capture_output=True, text=True, timeout=60)
            self.assertEqual(r.returncode, 0, msg="exit %d\n%s\n%s" % (r.returncode, r.stdout, r.stderr))
            self.assertTrue(os.path.exists(out), "no screenshot was written")
            with open(out, "rb") as f:
                result = pp.analyze(*pp.decode_png(f.read()))
        self.assertGreaterEqual(result["width"], 320)
        self.assertGreaterEqual(result["height"], 200)
        # The first hardware run rendered every surface pure black (pixel-shader constant buffer unbound).
        self.assertLess(result["black"], 0.005, msg="surfaces render black: %r" % result)
        # The lit ground plane and objects cover a large part of the viewport.
        self.assertGreater(result["lit"], 0.10, msg="too little lit geometry: %r" % result)


if __name__ == "__main__":
    unittest.main()
