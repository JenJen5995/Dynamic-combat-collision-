"""Build a tiny SSE .esp: start-game-enabled quest using MCM_ConfigBase (same layout as CloseCombatCamera.esp)."""

from __future__ import annotations

import struct
from pathlib import Path

AUTHOR = "typiak"
DESC = "MCM quest for Dynamic Combat Collision. Requires MCM Helper and SkyUI."
EDID = "DynamicCombatCollisionMCMQuest"
FULL = "Dynamic Combat Collision"
MOD_NAME = "DynamicCombatCollision"
FORM_ID = 0x01000800
VERSION = 44


def zstring(text: str) -> bytes:
    return text.encode("latin-1") + b"\x00"


def wstring(text: str) -> bytes:
    raw = text.encode("latin-1")
    return struct.pack("<H", len(raw)) + raw


def sub(name: str, data: bytes) -> bytes:
    return name.encode("ascii") + struct.pack("<H", len(data)) + data


def record(name: str, form_id: int, flags: int, data: bytes) -> bytes:
    header = name.encode("ascii")
    header += struct.pack("<I", len(data))
    header += struct.pack("<I", flags)
    header += struct.pack("<I", form_id)
    header += struct.pack("<I", 0)
    header += struct.pack("<HH", VERSION, 0)
    return header + data


def main() -> None:
    hed = struct.pack("<fII", 1.7, 2, 0x801)
    tes4_data = b"".join(
        [
            sub("HEDR", hed),
            sub("CNAM", zstring(AUTHOR)),
            sub("SNAM", zstring(DESC)),
            sub("MAST", zstring("Skyrim.esm")),
            sub("DATA", b"\x00" * 8),
        ]
    )
    tes4 = record("TES4", 0, 0x200, tes4_data)

    vmad = b"".join(
        [
            struct.pack("<HH", 5, 2),
            struct.pack("<H", 1),
            wstring("MCM_ConfigBase"),
            b"\x00",
            struct.pack("<H", 1),
            wstring("ModName"),
            b"\x02\x01",
            wstring(MOD_NAME),
            b"\x02\x00\x00\x00\x00\x00\x00",
        ]
    )
    dnam = b"\x01\x00\x00\xff" + b"\x00" * 8
    qust_data = b"".join(
        [
            sub("EDID", zstring(EDID)),
            sub("VMAD", vmad),
            sub("FULL", zstring(FULL)),
            sub("DNAM", dnam),
            sub("NEXT", b""),
        ]
    )
    qust = record("QUST", FORM_ID, 0, qust_data)

    grup_label = b"QUST"
    grup_data = qust
    grup = b"GRUP" + struct.pack("<I", 24 + len(grup_data)) + grup_label + struct.pack("<I", 0)
    grup += struct.pack("<I", 0)
    grup += struct.pack("<HH", 0, 0)
    grup += grup_data

    out = Path(__file__).resolve().parents[1] / "data" / "DynamicCombatCollision.esp"
    out.write_bytes(tes4 + grup)
    print(f"Wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
