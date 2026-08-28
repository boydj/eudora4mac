#!/usr/bin/env python3
"""Regenerate the app's Eudora.icns from the classic icon art.

The legacy icon *sources* (Icons/, Eudora60Icons/, new_icons/) lost their
resource forks when the code was archived, so the only surviving art is the
compiled icns inside the legacy app bundle.  This tool decodes the classic
elements (it32/t8mk 128px, ih32/h8mk 48px, il32/l8mk 32px, is32/s8mk 16px),
upscales the 128px master with a bicubic filter for the Retina sizes, and
writes a modern PNG-element icns.

Standard library only (no PIL); it is a one-time build step, and the result
is committed, so speed is irrelevant.

Usage:
    python3 tools/make_icon.py [source.icns] [output.icns]
"""

import struct
import sys
import zlib

DEFAULT_SOURCE = "EudoraDotApp/Contents/Resources/Eudora.icns"
DEFAULT_OUTPUT = "swift/EudoraApp/Resources/Eudora.icns"


# ---- icns container ---------------------------------------------------------

def parse_icns(data):
    if data[:4] != b"icns":
        raise SystemExit("not an icns file")
    total = struct.unpack(">I", data[4:8])[0]
    elems = {}
    off = 8
    while off + 8 <= min(total, len(data)):
        typ = data[off:off + 4].decode("latin-1")
        length = struct.unpack(">I", data[off + 4:off + 8])[0]
        if length < 8 or off + length > len(data):
            break
        elems[typ] = data[off + 8:off + length]
        off += length
    return elems


def build_icns(elements):
    """elements: list of (type, data) in the order they should appear."""
    body = b"".join(
        typ.encode("ascii") + struct.pack(">I", len(data) + 8) + data
        for typ, data in elements)
    return b"icns" + struct.pack(">I", len(body) + 8) + body


# ---- classic element decoding ----------------------------------------------

def unpack_rle(data, expected):
    """The icns PackBits variant: control < 0x80 is a literal run of
    control+1 bytes; control >= 0x80 repeats the next byte control-0x80+3
    times.  (No -128 no-op, unlike true PackBits.)"""
    out = bytearray()
    i = 0
    n = len(data)
    while len(out) < expected and i < n:
        c = data[i]
        i += 1
        if c < 0x80:
            run = c + 1
            out += data[i:i + run]
            i += run
        else:
            if i >= n:
                break
            out += bytes([data[i]]) * (c - 0x80 + 3)
            i += 1
    if len(out) < expected:
        raise ValueError("RLE stream too short (%d of %d)" % (len(out), expected))
    return bytes(out[:expected])


def decode_classic_rgba(rgb_data, mask_data, size):
    """Decode an is32/il32/ih32/it32 element plus its 8-bit mask to RGBA."""
    npix = size * size
    if len(rgb_data) == npix * 4:
        # Uncompressed: interleaved ARGB, alpha byte unused (mask rules).
        planes = [bytes(rgb_data[c::4]) for c in (1, 2, 3)]
    else:
        data = rgb_data
        # it32 carries a 4-byte (zero) header before the RLE stream.
        for skip in ((4, 0) if data[:4] == b"\0\0\0\0" else (0,)):
            try:
                flat = unpack_rle(data[skip:], npix * 3)
                break
            except ValueError:
                continue
        else:
            raise SystemExit("cannot decode %dpx RGB element" % size)
        planes = [flat[0:npix], flat[npix:2 * npix], flat[2 * npix:3 * npix]]
    if len(mask_data) < npix:
        raise SystemExit("mask for %dpx element too short" % size)
    rgba = bytearray(npix * 4)
    rgba[0::4] = planes[0]
    rgba[1::4] = planes[1]
    rgba[2::4] = planes[2]
    rgba[3::4] = mask_data[:npix]
    return bytes(rgba)


# ---- resampling -------------------------------------------------------------

def _cubic(t, a=-0.5):
    # Catmull-Rom (the usual "bicubic" kernel).
    t = abs(t)
    if t <= 1.0:
        return (a + 2.0) * t * t * t - (a + 3.0) * t * t + 1.0
    if t < 2.0:
        return a * t * t * t - 5.0 * a * t * t + 8.0 * a * t - 4.0 * a
    return 0.0


def _taps(src_n, dst_n):
    """Per destination index: ([4 source indices], [4 normalized weights])."""
    taps = []
    scale = src_n / dst_n
    for d in range(dst_n):
        center = (d + 0.5) * scale - 0.5
        base = int(center // 1)
        idxs, wts = [], []
        for k in range(base - 1, base + 3):
            w = _cubic(center - k)
            idxs.append(min(max(k, 0), src_n - 1))
            wts.append(w)
        total = sum(wts)
        taps.append((idxs, [w / total for w in wts]))
    return taps


def _resize_plane(plane, sw, sh, dw, dh):
    """Separable bicubic resize of one float plane (row-major list)."""
    xt = _taps(sw, dw)
    # Horizontal pass: sh rows of dw.
    mid = [0.0] * (sh * dw)
    for y in range(sh):
        row = plane[y * sw:(y + 1) * sw]
        out = y * dw
        for x, (idxs, wts) in enumerate(xt):
            mid[out + x] = (row[idxs[0]] * wts[0] + row[idxs[1]] * wts[1] +
                            row[idxs[2]] * wts[2] + row[idxs[3]] * wts[3])
    yt = _taps(sh, dh)
    dst = [0.0] * (dw * dh)
    for y, (idxs, wts) in enumerate(yt):
        r0, r1 = idxs[0] * dw, idxs[1] * dw
        r2, r3 = idxs[2] * dw, idxs[3] * dw
        w0, w1, w2, w3 = wts
        out = y * dw
        for x in range(dw):
            dst[out + x] = (mid[r0 + x] * w0 + mid[r1 + x] * w1 +
                            mid[r2 + x] * w2 + mid[r3 + x] * w3)
    return dst


def resize_rgba(rgba, src, dst):
    """Bicubic resize in premultiplied-alpha space (no fringe from RGB under
    transparent pixels)."""
    npix = src * src
    alpha = [rgba[i * 4 + 3] / 255.0 for i in range(npix)]
    planes = []
    for c in range(3):
        planes.append([rgba[i * 4 + c] * alpha[i] for i in range(npix)])
    planes.append([a * 255.0 for a in alpha])

    scaled = [_resize_plane(p, src, src, dst, dst) for p in planes]

    out = bytearray(dst * dst * 4)
    ap = scaled[3]
    for i in range(dst * dst):
        a = ap[i]
        a_clamped = min(max(a, 0.0), 255.0)
        out[i * 4 + 3] = int(a_clamped + 0.5)
        if a > 1e-6:
            for c in range(3):
                v = scaled[c][i] * 255.0 / a
                out[i * 4 + c] = int(min(max(v, 0.0), 255.0) + 0.5)
    return bytes(out)


def box_halve(rgba, src):
    """Exact 2x2 box downsample (premultiplied), for the 64px slot."""
    dst = src // 2
    out = bytearray(dst * dst * 4)
    for y in range(dst):
        for x in range(dst):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in (2 * y, 2 * y + 1):
                for sx in (2 * x, 2 * x + 1):
                    o = (sy * src + sx) * 4
                    a = rgba[o + 3] / 255.0
                    acc[0] += rgba[o] * a
                    acc[1] += rgba[o + 1] * a
                    acc[2] += rgba[o + 2] * a
                    acc[3] += a
            o = (y * dst + x) * 4
            a = acc[3] / 4.0
            out[o + 3] = int(min(a * 255.0, 255.0) + 0.5)
            if a > 1e-6:
                for c in range(3):
                    out[o + c] = int(min(max(acc[c] / 4.0 / a, 0.0), 255.0) + 0.5)
    return bytes(out)


# ---- PNG --------------------------------------------------------------------

def write_png(size, rgba):
    raw = b"".join(b"\x00" + rgba[y * size * 4:(y + 1) * size * 4]
                   for y in range(size))

    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) +
            chunk(b"IEND", b""))


def png_dimensions(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", data[16:24])


# ---- main -------------------------------------------------------------------

def main():
    source = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SOURCE
    output = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUTPUT

    with open(source, "rb") as f:
        elems = parse_icns(f.read())

    def classic(rgb_type, mask_type, size):
        if rgb_type not in elems or mask_type not in elems:
            raise SystemExit("source lacks %s/%s" % (rgb_type, mask_type))
        return decode_classic_rgba(elems[rgb_type], elems[mask_type], size)

    art16 = classic("is32", "s8mk", 16)
    art32 = classic("il32", "l8mk", 32)
    art128 = classic("it32", "t8mk", 128)

    art64 = box_halve(art128, 128)
    print("upscaling 128 -> 256 ...")
    art256 = resize_rgba(art128, 128, 256)
    print("upscaling 128 -> 512 ...")
    art512 = resize_rgba(art128, 128, 512)
    print("upscaling 128 -> 1024 ...")
    art1024 = resize_rgba(art128, 128, 1024)

    out_elements = [
        ("icp4", write_png(16, art16)),     # 16
        ("icp5", write_png(32, art32)),     # 32
        ("ic11", write_png(32, art32)),     # 16@2x
        ("ic12", write_png(64, art64)),     # 32@2x
        ("ic07", write_png(128, art128)),   # 128
        ("ic13", write_png(256, art256)),   # 128@2x
        ("ic08", write_png(256, art256)),   # 256
        ("ic14", write_png(512, art512)),   # 256@2x
        ("ic09", write_png(512, art512)),   # 512
        ("ic10", write_png(1024, art1024)), # 512@2x
    ]

    blob = build_icns(out_elements)
    with open(output, "wb") as f:
        f.write(blob)

    # Verify by re-parsing what was written.
    check = parse_icns(blob)
    print("wrote %s (%d bytes):" % (output, len(blob)))
    for typ, data in check.items():
        dims = png_dimensions(data)
        print("  %s  %7d bytes  %s" %
              (typ, len(data), "%dx%d PNG" % dims if dims else "not PNG!"))
        if not dims:
            raise SystemExit("element %s is not a PNG" % typ)


if __name__ == "__main__":
    main()
