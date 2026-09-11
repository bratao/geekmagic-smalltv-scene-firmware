"""Read-only structural verification of this ESP8266 4MB/2MB-FS OTA image.

Does not establish runtime correctness or communicate with a device.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess


def check(condition, message):
    if not condition:
        raise ValueError(message)


def verify(binary, elf, nm, elf2bin):
    raw = binary.read_bytes()
    check(4120 < len(raw) <= 1044464, "Binary outside sketch limit")
    masked = bytearray(raw)
    length, crc = struct.unpack_from("<II", raw, 4112)
    masked[4112:4120] = bytes(8)
    spec = importlib.util.spec_from_file_location("elf2bin", elf2bin)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    check(length == len(raw), "Embedded image length mismatch")
    check(module.crc8266(masked) == crc, "Full image CRC mismatch")
    images = []
    for base, expected_count in ((0, 2), (4096, 5)):
        magic, count, mode, flash, entry = struct.unpack_from("<BBBBI", raw, base)
        check((magic, count, mode, flash) == (0xE9, expected_count, 2, 0x40), "Unexpected image header")
        check(0x40100000 <= entry < 0x40110000, "Entry outside instruction RAM")
        pos, xor, segments = base + 8, 0xEF, []
        for _ in range(count):
            check(pos + 8 <= len(raw), "Truncated segment header")
            address, size = struct.unpack_from("<II", raw, pos)
            pos += 8
            check(pos + size <= len(raw), "Truncated segment")
            allowed = ((0x3FFE8000, 0x40000000), (0x40100000, 0x40110000), (0x40201010, 0x402FFFFF))
            check(any(lo <= address and address + size <= hi for lo, hi in allowed), "Segment outside expected memory")
            for byte in masked[pos:pos + size]:
                xor ^= byte
            segments.append({"address": hex(address), "bytes": size})
            pos += size
        end = base + ((pos - base) // 16) * 16 + 15
        check(end < len(raw) and raw[end] == xor, "Segment XOR mismatch")
        check(end < 4096 if base == 0 else end == len(raw) - 1, "Unexpected image ending")
        images.append({"offset": base, "entry": hex(entry), "checksum": hex(xor), "segments": segments})
    listing = subprocess.check_output([str(nm), "-S", "-C", str(elf)], text=True)
    symbols = {}
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) == 4:
            symbols[fields[3]] = (int(fields[0], 16), int(fields[1], 16))
        elif len(fields) == 3:
            symbols[fields[2]] = (int(fields[0], 16), 0)
    for name, address in {"_FS_start": 0x40400000, "_FS_end": 0x405FA000, "_EEPROM_start": 0x405FB000}.items():
        check(symbols.get(name, (None,))[0] == address, f"Layout changed: {name}")
    tables = {}
    for name in ("Y2I16", "CR2R16", "CR2G16", "CB2G16", "CB2B16", "CLIPRBE", "CLIPGBE", "CLIPBBE"):
        address, size = symbols[name]
        check(0x40200000 <= address < 0x40300000, f"Table still occupies RAM: {name}")
        tables[name] = {"address": hex(address), "bytes": size}
    check(sum(value["bytes"] for value in tables.values()) == 6804, "Unexpected table total")
    return {"passed": True, "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(),
            "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(), "crc": hex(crc),
            "flash_mode": "DIO", "flash_size": "4MB", "flash_frequency": "40MHz",
            "sketch_limit": 1044464, "images": images, "flash_tables": tables,
            "limitations": "Structural integrity only; device OTA space, heap, SPI and Wi-Fi require live verification."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for name in ("binary", "elf", "nm", "elf2bin"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(verify(args.binary, args.elf, args.nm, args.elf2bin), indent=2))
