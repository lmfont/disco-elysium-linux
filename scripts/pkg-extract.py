#!/usr/bin/env python3
"""pkg-extract.py — extract a subset of a Unity .pkg (xar) into a directory.

Unity's Mac module packages are xar archives whose Payload member is a gzip
compressed cpio archive. This uses only the Python standard library, so the
run script does not depend on bsdtar/7z.

Usage: pkg-extract.py PACKAGE.pkg PREFIX OUT_DIR
       (extracts Payload entries under PREFIX, e.g.
        Variations/linux64_withgfx_nondevelopment_mono)
"""
import io
import os
import struct
import sys
import zlib


class PayloadReader:
    """Streaming gzip reader over a byte range of the xar file."""

    def __init__(self, fileobj, length):
        self.fileobj = fileobj
        self.remaining = length
        self.decomp = zlib.decompressobj(16 + zlib.MAX_WBITS)
        self.buf = b""

    def _fill(self, n):
        while len(self.buf) < n and self.remaining > 0:
            chunk = self.fileobj.read(min(1 << 20, self.remaining))
            if not chunk:
                break
            self.remaining -= len(chunk)
            self.buf += self.decomp.decompress(chunk)
        if self.remaining == 0:
            self.buf += self.decomp.flush()

    def read_exact(self, n):
        self._fill(n)
        if len(self.buf) < n:
            return None
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def read_into(self, n, dest):
        remaining = n
        while remaining > 0:
            take = min(remaining, 1 << 20)
            data = self.read_exact(take)
            if data is None:
                raise EOFError("payload ended early")
            dest.write(data)
            remaining -= len(data)


def find_payload(pkg_path):
    with open(pkg_path, "rb") as f:
        header = f.read(28)
        magic, _hsize, _ver, toc_c, _toc_u, _ck = struct.unpack(">4sHHQQI", header)
        if magic != b"xar!":
            raise SystemExit(f"{pkg_path}: not a xar archive")
        toc = zlib.decompress(f.read(toc_c))
        heap_start = 28 + toc_c
    import xml.etree.ElementTree as ET

    root = ET.fromstring(toc)
    for file_el in root.iter("file"):
        if file_el.findtext("name") != "Payload":
            continue
        data = file_el.find("data")
        return heap_start + int(data.findtext("offset")), int(data.findtext("length"))
    raise SystemExit(f"{pkg_path}: no Payload member")


def extract(pkg_path, prefix, out_dir):
    payload_off, payload_len = find_payload(pkg_path)
    prefix = prefix.strip("/")
    written = 0

    with open(pkg_path, "rb") as f:
        f.seek(payload_off)
        reader = PayloadReader(f, payload_len)
        while True:
            hdr = reader.read_exact(6)
            if hdr is None:
                raise SystemExit("cpio stream ended without TRAILER")
            if hdr == b"070701" or hdr == b"070702":  # newc: 8-hex fields
                rest = reader.read_exact(110 - 6)
                fields = [int(rest[i * 8:(i + 1) * 8], 16) for i in range(13)]
                mode, filesize, namesize = fields[1], fields[6], fields[11]
                name = reader.read_exact(namesize)
                name = name.split(b"\x00", 1)[0].decode("utf-8", "surrogateescape")
                reader.read_exact((-namesize) % 4)
                data_pad = (-filesize) % 4
            elif hdr == b"070707":  # odc: 76-byte header, 6/11-char octal fields
                rest = reader.read_exact(70)
                mode = int(rest[12:18], 8)     # 18-6
                namesize = int(rest[53:59], 8)  # 59-6
                filesize = int(rest[59:70], 8)  # 65-6
                name = reader.read_exact(namesize)
                name = name.split(b"\x00", 1)[0].decode("utf-8", "surrogateescape")
                data_pad = 0
            else:
                raise SystemExit(f"bad cpio magic: {hdr!r}")
            if name == "TRAILER!!!":
                break
            norm = name[2:] if name.startswith("./") else name
            if norm.startswith(prefix):
                rel = norm[len(prefix):].lstrip("/")
                if rel:
                    dest = os.path.join(out_dir, rel)
                    kind = mode & 0o170000
                    if kind == 0o040000:
                        os.makedirs(dest, exist_ok=True)
                        reader.read_into(filesize, open(os.devnull, "wb"))
                    elif kind == 0o120000:
                        link_target = reader.read_exact(filesize).decode("utf-8", "surrogateescape")
                        reader.read_exact(data_pad)
                        os.makedirs(os.path.dirname(dest), exist_ok=True)
                        if os.path.lexists(dest):
                            os.remove(dest)
                        os.symlink(link_target, dest)
                        continue
                    else:
                        os.makedirs(os.path.dirname(dest), exist_ok=True)
                        with open(dest, "wb") as out:
                            reader.read_into(filesize, out)
                        os.chmod(dest, mode & 0o7777)
                        written += 1
                else:
                    reader.read_into(filesize, open(os.devnull, "wb"))
            else:
                reader.read_into(filesize, open(os.devnull, "wb"))
            reader.read_exact(data_pad)

    print(f"pkg-extract: wrote {written} files under {out_dir}")
    return 0


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    sys.exit(extract(sys.argv[1], sys.argv[2], sys.argv[3]))


if __name__ == "__main__":
    main()
