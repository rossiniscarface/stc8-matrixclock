"""Reads the ESP32-Cam's MJPEG stream in a background thread.

Single requests via the snapshot port turned out to be useless: the server
gave up with HTTP 500 under load, and above all it delivered stale images -
in the first scan attempt the detected lit position stayed the same over
dozens of pin pairs, because the same old frame kept coming.

The stream keeps one connection open and delivers about 8 images per second.
`frame_nach()` is important: it only returns once an image is available that
was demonstrably taken after the given point in time.
"""
import io
import re
import threading
import time
import urllib.request

import numpy as np
from PIL import Image


class Kamera:
    def __init__(self, url, timeout=10):
        self.url = url
        self.timeout = timeout
        self._sperre = threading.Lock()
        self._zeit = 0.0
        self._bild = None
        self._laeuft = True
        self._fehler = None
        self._faden = threading.Thread(target=self._lesen, daemon=True)
        self._faden.start()

    def _lesen(self):
        while self._laeuft:
            try:
                r = urllib.request.urlopen(self.url, timeout=self.timeout)
                ct = r.headers.get("Content-Type", "")
                m = re.search(r"boundary=(\S+)", ct)
                if not m:
                    raise RuntimeError(f"no boundary in {ct!r}")
                bnd = ("--" + m.group(1).strip('"')).encode()
                puffer = b""
                while self._laeuft:
                    stueck = r.read(8192)
                    if not stueck:
                        break
                    puffer += stueck
                    while True:
                        i = puffer.find(bnd)
                        if i < 0:
                            break
                        j = puffer.find(b"\r\n\r\n", i)
                        if j < 0:
                            break
                        cl = re.search(rb"Content-Length:\s*(\d+)", puffer[i:j], re.I)
                        if not cl:
                            puffer = puffer[j + 4:]
                            continue
                        n = int(cl.group(1))
                        if len(puffer) < j + 4 + n:
                            break
                        jpg = puffer[j + 4:j + 4 + n]
                        puffer = puffer[j + 4 + n:]
                        try:
                            a = np.asarray(
                                Image.open(io.BytesIO(jpg)).convert("L"),
                                dtype=np.float32)
                        except Exception:
                            continue
                        with self._sperre:
                            self._zeit = time.time()
                            self._bild = a
                r.close()
            except Exception as e:
                self._fehler = e
                time.sleep(1.0)

    def frame_nach(self, t, warten=6.0):
        """Waits for an image taken after t."""
        ende = time.time() + warten
        while time.time() < ende:
            with self._sperre:
                if self._bild is not None and self._zeit > t:
                    return self._bild.copy()
            time.sleep(0.01)
        raise TimeoutError(f"no fresh image ({self._fehler})")

    def frame_nach_mit_zeit(self, t, warten=6.0):
        """Like frame_nach, but also returns the capture time.

        The caller can use it to check whether the image still fell into the
        window in which the LED was actually lit.
        """
        ende = time.time() + warten
        while time.time() < ende:
            with self._sperre:
                if self._bild is not None and self._zeit > t:
                    return self._bild.copy(), self._zeit
            time.sleep(0.01)
        raise TimeoutError(f"no fresh image ({self._fehler})")

    def mittelwert(self, n=5):
        """Averages n consecutive images - a calmer reference."""
        summe = None
        for _ in range(n):
            f = self.frame_nach(time.time())
            summe = f if summe is None else summe + f
        return summe / n

    def stop(self):
        self._laeuft = False
