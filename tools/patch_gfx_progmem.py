"""Move Arduino_GFX 1.6.7 YCbCr tables to ESP8266 flash, preserving conversion.

Run with the installed library directory as the sole argument. Exact SHA256
checks reject other dependency versions or local edits before writing anything.
Other architectures retain ordinary const tables and ordinary array reads.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
import re
import sys


ORIGINAL = {
    "src/YCbCr2RGB.h": "5b75c28bf83a8eef05a85919c08175696e9920100f4b9736a759d6b6014458b7",
    "src/Arduino_DataBus.cpp": "342d946717361402f7cf2d1af437ee796deadbcbe7bcfe84a29debff3d9d0f65",
}
PATCHED = {
    "src/YCbCr2RGB.h": "6d229f1c02bb460ffa1c8c4df914a236dff26196c7801917b5c173e10147f259",
    "src/Arduino_DataBus.cpp": "7284eb11526e874a49693bbb5e0c43d6ee669239ec5f4a065b194a581a4f8feb",
}
TABLES = ("Y2I16", "CR2R16", "CR2G16", "CB2G16", "CB2B16", "CLIPRBE", "CLIPGBE", "CLIPBBE")


def transform(name: str, raw: bytes) -> bytes:
    text = raw.decode("utf-8")
    if name.endswith("YCbCr2RGB.h"):
        text = text.replace("#pragma once", """#pragma once

// SmallTV: keep these 6804 bytes out of scarce ESP8266 DRAM.
#if defined(ESP8266)
#include <pgmspace.h>
#define SMALLTV_YCBCR_STORAGE PROGMEM
#else
#define SMALLTV_YCBCR_STORAGE
#endif""", 1)
        for table in TABLES:
            text, count = re.subn(r"(static const (?:u?int16_t) " + table + r"\[\])", r"\1 SMALLTV_YCBCR_STORAGE", text)
            if count != 1:
                raise ValueError(f"Expected exactly one declaration: {table}")
        text += "\n#undef SMALLTV_YCBCR_STORAGE\n"
    else:
        text = text.replace('#include "Arduino_DataBus.h"', '''#include "Arduino_DataBus.h"

// Signed intermediate tables must preserve int16_t sign after flash reads.
#if defined(ESP8266)
#include <pgmspace.h>
#define SMALLTV_YCBCR_READ(table, index) static_cast<int16_t>(pgm_read_word(&(table)[index]))
#define SMALLTV_YCBCR_READ_U(table, index) pgm_read_word(&(table)[index])
#else
#define SMALLTV_YCBCR_READ(table, index) ((table)[index])
#define SMALLTV_YCBCR_READ_U(table, index) ((table)[index])
#endif''', 1)
        for table in TABLES:
            macro = "SMALLTV_YCBCR_READ_U" if table.startswith("CLIP") else "SMALLTV_YCBCR_READ"
            text, count = re.subn(r"\b" + table + r"\[([^\]]+)\]", lambda m: f"{macro}({table}, {m[1]})", text)
            expected = 2 if table == "Y2I16" or table.startswith("CLIP") else 1
            if count != expected:
                raise ValueError(f"Unexpected reads for {table}: {count}")
        text += "\n#undef SMALLTV_YCBCR_READ\n#undef SMALLTV_YCBCR_READ_U\n"
    return text.encode("utf-8")


def apply(library: Path) -> None:
    pending = []
    for name, expected in ORIGINAL.items():
        path = library / name
        raw = path.read_bytes()
        actual = hashlib.sha256(raw).hexdigest()
        if actual == PATCHED[name]:
            print(f"already patched {name}: {actual}")
            continue
        if actual != expected:
            raise ValueError(f"Refusing unknown Arduino_GFX source {path}: SHA256 {actual}")
        result = transform(name, raw)
        after = hashlib.sha256(result).hexdigest()
        if after != PATCHED[name]:
            raise ValueError(f"Patch output checksum mismatch for {name}: {after}")
        pending.append((path, result, actual, after))
    for path, result, before, after in pending:
        path.write_bytes(result)
        print(f"patched {path.name}: {before} -> {after}")
    print("ESP8266 YCbCr tables: 6804 bytes moved from DRAM to flash; verify final ELF.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python patch_gfx_progmem.py <Arduino_GFX library directory>")
    apply(Path(sys.argv[1]))
