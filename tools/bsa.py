"""Minimal Fallout 3 / New Vegas BSA (version 104) reader: list and extract files."""
import struct, zlib

class Archive:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            magic, version, offset, flags, folders, files, folder_names, file_names, file_flags = struct.unpack('<4s8I', f.read(36))
            assert magic == b'BSA\0' and version == 104, path
            self.flags = flags
            records = [struct.unpack('<QII', f.read(16)) for _ in range(folders)]
            entries = []
            for _hash, count, _off in records:
                name = b''
                if flags & 1:
                    length = f.read(1)[0]
                    name = f.read(length)[:-1]
                for _ in range(count):
                    _h, size, data = struct.unpack('<QII', f.read(16))
                    entries.append([name.decode('latin-1'), size, data])
            names = f.read(file_names).split(b'\0') if flags & 2 else []
            self.entries = {}
            for entry, name in zip(entries, names):
                path_ = (entry[0] + '\\' + name.decode('latin-1')).lower()
                self.entries[path_] = (entry[1], entry[2])

    def read(self, name):
        size, offset = self.entries[name.lower()]
        compressed = bool(self.flags & 4) ^ bool(size & 0x40000000)
        size &= 0x3FFFFFFF
        with open(self.path, 'rb') as f:
            f.seek(offset)
            raw = f.read(size)
        if self.flags & 0x100:
            raw = raw[1 + raw[0]:]
        if compressed:
            raw = zlib.decompress(raw[4:])
        return raw
