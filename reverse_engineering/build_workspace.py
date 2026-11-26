#!/usr/bin/env python3
"""Build the Intellifex LTD reverse-engineering workspace from the 27C256 EPROM.

Finds and verifies the EPROM by SHA-256, re-derives its layout (8051 program,
DSP bootstrap, shared DSP code and six DSP configurations), writes the records
and a manifest, disassembles both processors and writes the memory map, entry
points, seeds and direct-call graph under the output directory.
"""

import argparse
import csv
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import NamedTuple

import mcs51_disasm

RE_ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = RE_ROOT.parent
INPUT_DIRS = (PROJECT_ROOT, RE_ROOT / "inputs")
DEFAULT_OUTPUT = RE_ROOT
DEFAULT_DISASSEMBLER = "dsp56300-disasm"
DISASSEMBLER_TIMEOUT = 600

EPROM_SIZE = 0x8000
EPROM_SHA256 = "7f6162d9e77e9b63783df7a8158c646d287e746de1ca56bdbea63b92e60004b1"
MANUAL_SHA256 = "bda911de8908853575a79bcaae32d5dfd99ec52ca7656b004df992f7ee16034e"

WORD = 3
MCU = "8051"
DSP = "DSP56001"

MCU_START = 0x0000
MCU_END = 0x4AEF
BOOT_HEADER = 0x4AF0
FILL_START = 0x7EEF
DSP_BOOT_COUNT = 211
DSP_SHARED_RANGE = (0x0400, 0x0A0F)
DSP_CFG_LOAD = 0x00A0
DSP_CFG_RANGE = (0x00A0, 0x03FF)
# The reset code at P:$0097 runs on into P:$00A0, which every configuration replaces with
# its own code, so it exists in the bootstrap runtime only.
DSP_BOOT_ONLY = frozenset({0x0097})

MCU_PROGRAM = "mcu"
DSP_CFG_PROGRAMS = ";".join(f"dsp_cfg{index}" for index in range(6))
ALL_DSP_PROGRAMS = ";".join(["dsp_boot", *DSP_CFG_PROGRAMS.split(";")])

CONFIG_NAMES = {
    0: "Hush;Chorus;Delay;Reverb",
    1: "Hush;Pitch Shift;Delay;Reverb",
    2: "Hush;Reverb",
    3: "Hush;Delay;Ducker",
    4: "Hush;8 Voice Chorus;Delay",
    5: "Hush;Pitch Shift;Delay",
}

# Header offset, load address, word count and configuration index of the load
# images, in EPROM order, as verified by hand against 0x0F2F's switch table and
# the configuration-name table at 0x45A4.
EXPECTED_IMAGES = (
    (0x4D6B, 0x0400, 1552, None),
    (0x5F9F, 0x00A0, 602, 2),
    (0x66B1, 0x00A0, 363, 4),
    (0x6AF6, 0x00A0, 357, 5),
    (0x6F29, 0x00A0, 263, 3),
    (0x7242, 0x00A0, 534, 0),
    (0x7888, 0x00A0, 545, 1),
)

# Filled in from the split and checked on every run.
RECORD_SHA256 = {
    "00_mcu_0000_19184b.bin": "9befed5aa84813388edbf631859c9b20290de6565f24eadfdbf92d60dd07212c",
    "01_dsp_boot_p0000_211w.bin": "b531d9b3be9f7a0a154202fb43905cb462c66b7b6e806e04842eedf8fe98b91c",
    "02_dsp_shared_p0400_1552w.bin": "b5194a0259ac6f590096153ea8d0bb5e8148f35d376fe05521d599b878a03ce0",
    "03_dsp_cfg2_p00a0_602w.bin": "88f9370b1d4e605a2ba3727f0c5add20a767bba4626f2d774b0ef3f12c721666",
    "04_dsp_cfg4_p00a0_363w.bin": "1920d319dcacc25bbe88cbcf3f0e859fd63f7c76c8cb490075005e5074d49aed",
    "05_dsp_cfg5_p00a0_357w.bin": "9aec81d4fc003dea0892b2fecbdedf434a0f181867710dce4d26ec3b88022575",
    "06_dsp_cfg3_p00a0_263w.bin": "5c1008c9ae64f5875f162618c211bb3c752117a46536bea08e0087162f881a37",
    "07_dsp_cfg0_p00a0_534w.bin": "fdbb21588ae24484d308a3266bc6f5baa66b3e1f9186e87e9839b97aaf2cc64d",
    "08_dsp_cfg1_p00a0_545w.bin": "bd2de452ec4a0bffa95abc54dced849367253426bfd40180b956b414be9246f6",
}

MCU_ENTRY_POINTS = (
    ("mcu_reset", 0x0000),
    ("mcu_timer0", 0x000B),
    ("mcu_serial", 0x0023),
)
MCU_UNUSED_VECTORS = (0x0003, 0x0013, 0x001B)

MCU_ENTRY_EVIDENCE = {
    "mcu_reset": (
        "8051 reset vector: ljmp 0x0026; vectors 0x0003, 0x0013 and 0x001B "
        "hold 0xFFFF (unused, not seeded)"
    ),
    "mcu_timer0": "timer-0 vector: clr TR0; ljmp 0x14E3",
    "mcu_serial": "serial vector: ljmp 0x1A3A",
}


class Vector(NamedTuple):
    slot: int
    name: str
    kind: str
    target: int | None


# Two-word DSP56001 vector slots; only the slots this boot image uses.
DSP_VECTORS = (
    Vector(0x00, "dsp_reset", "jmp", 0x0097),
    Vector(0x0C, "dsp_ssi_rx", "fast", None),
    Vector(0x0E, "dsp_ssi_rx_exception", "fast", None),
    Vector(0x10, "dsp_ssi_tx", "fast", None),
    Vector(0x12, "dsp_ssi_tx_exception", "jsr", 0x0014),
    Vector(0x20, "dsp_host_rx", "fast", None),
    Vector(0x24, "dsp_host_command", "jsr", 0x005E),
    Vector(0x26, "dsp_host_command_26", "fast", None),
    Vector(0x28, "dsp_host_command_28", "fast", None),
    Vector(0x2A, "dsp_host_command_2a", "jsr", 0x0480),
    Vector(0x2C, "dsp_host_command_2c", "fast", None),
    Vector(0x2E, "dsp_host_command_2e", "jsr", 0x0490),
    Vector(0x30, "dsp_host_command_30", "jsr", 0x0075),
    Vector(0x32, "dsp_host_command_32", "jsr", 0x0063),
)
DSP_UNUSED_VECTORS = (0x02, 0x04, 0x06, 0x08, 0x0A, 0x1E, 0x22)

DSP_JSR = 0x0BF080
DSP_JMP = 0x0AF080

# name, address of DSP entry points the vectors and cross-image calls name.
DSP_ENTRY_POINTS = (
    ("dsp_reset_code", 0x0097),
    ("ssi_tx_exception_handler", 0x0014),
    ("hc_0024_handler", 0x005E),
    ("hc_002a_handler", 0x0480),
    ("hc_002e_handler", 0x0490),
    ("hc_0030_handler", 0x0075),
    ("hc_0032_handler", 0x0063),
    ("dsp_main_loop", 0x0038),
    ("dsp_shared_entry", 0x0400),
    ("dsp_configuration_entry", 0x00A0),
)

DSP_ENTRY_EVIDENCE = {
    "dsp_reset_code": "target of `jmp $0097` in the reset slot P:$0000",
    "ssi_tx_exception_handler": "`jsr $0014` in the SSI transmit exception slot",
    "hc_0024_handler": "`jsr $005e` in the host command slot P:$0024",
    "hc_002a_handler": "`jsr $0480` in the host command slot P:$002A",
    "hc_002e_handler": "`jsr $0490` in the host command slot P:$002E",
    "hc_0030_handler": "`jsr $0075` in the host command slot P:$0030",
    "hc_0032_handler": "`jsr $0063` in the host command slot P:$0032",
    "dsp_main_loop": "main loop: `jmp $0038` at P:$003E",
    "dsp_shared_entry": "reached by `jset #10,x:$0000,$0400` at P:$003C",
    "dsp_configuration_entry": "called by `jsclr #11,x:$0000,$00a0` at shared $0447",
}

# site, target, mnemonic for the two cross-image entries claimed above.
DSP_CROSS_ENTRIES = (
    ("dsp_shared_entry", 0x003C, 0x0400, "jset"),
    ("dsp_configuration_entry", 0x0447, 0x00A0, "jsclr"),
)

DSP_INSTRUCTION = re.compile(r"^P:\$([0-9a-fA-F]{6,8})\s+[0-9a-fA-F]{6}\s+(.+)$")
DSP_OPERAND_WORD = re.compile(r"^[0-9a-fA-F]{6}$")
DSP_TARGET = re.compile(r"\$([0-9a-fA-F]+)$")


class Record(NamedTuple):
    filename: str
    region: str
    processor: str
    eprom_offset: int
    header_offset: int | None
    space: str
    load_address: int
    unit: str
    count: int
    size: int
    sha256: str
    notes: str
    programs: str

    @property
    def end(self) -> int:
        return self.eprom_offset + self.size - 1


class Entry(NamedTuple):
    name: str
    address: int
    processor: str
    programs: str
    evidence: str


class Call(NamedTuple):
    processor: str
    record: str
    site: int
    mnemonic: str
    target: int
    resolution: str


class DspInsn(NamedTuple):
    address: int
    mnemonic: str
    operand: str
    target: int | None


def digest(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()


def find_input(wanted: str) -> Path | None:
    for directory in INPUT_DIRS:
        if not directory.is_dir():
            continue
        for path in sorted(directory.iterdir()):
            if (
                path.is_file()
                and not path.name.startswith(".")
                and digest(path.read_bytes()) == wanted
            ):
                return path
    return None


def read_eprom() -> bytes:
    path = find_input(EPROM_SHA256)
    if path is None:
        raise SystemExit(
            f"EPROM not found: put the {EPROM_SIZE}-byte image with SHA-256 "
            f"{EPROM_SHA256} in {INPUT_DIRS[0]} or {INPUT_DIRS[1]}"
        )
    blob = path.read_bytes()
    if len(blob) != EPROM_SIZE:
        raise SystemExit(f"{path}: {len(blob)} bytes, expected {EPROM_SIZE}")
    print(f"EPROM: {path} ({len(blob)} bytes, sha256 {EPROM_SHA256})")
    return blob


def report_manual() -> None:
    path = find_input(MANUAL_SHA256)
    if path is None:
        print(f"manual: missing (SHA-256 {MANUAL_SHA256}); not needed to build")
    else:
        print(f"manual: {path}")


def programs_for(processor: str, address: int) -> str:
    if processor == MCU:
        return MCU_PROGRAM
    if address in DSP_BOOT_ONLY:
        return "dsp_boot"
    if address < DSP_CFG_LOAD:
        return ALL_DSP_PROGRAMS
    if address <= DSP_SHARED_RANGE[1]:
        return DSP_CFG_PROGRAMS
    return ""


def hex_address(address: int, processor: str = MCU) -> str:
    return f"0x{address:04x}" if processor == MCU else f"0x{address:06x}"


def split(image: bytes) -> list[Record]:
    """Walk the load headers and check the result against the known layout."""
    mcu = Record(
        filename=f"00_mcu_{MCU_START:04x}_{MCU_END - MCU_START + 1}b.bin",
        region="mcu",
        processor=MCU,
        eprom_offset=MCU_START,
        header_offset=None,
        space="CODE",
        load_address=MCU_START,
        unit="byte",
        count=MCU_END - MCU_START + 1,
        size=MCU_END - MCU_START + 1,
        sha256=digest(image[MCU_START : MCU_END + 1]),
        notes=(
            "8051 program and data: factory preset names at 0x1C54, UI strings "
            "0x39B0..0x4AE5, 10-byte table at 0x4AE6"
        ),
        programs=MCU_PROGRAM,
    )
    records = [mcu]

    offset = BOOT_HEADER
    count = int.from_bytes(image[offset : offset + 2], "big")
    if count != DSP_BOOT_COUNT:
        raise SystemExit(
            f"bootstrap at {offset:#06x} holds {count} words, expected {DSP_BOOT_COUNT}"
        )
    payload = offset + 2
    records.append(
        Record(
            filename=f"01_dsp_boot_p0000_{count}w.bin",
            region="dsp_boot",
            processor=DSP,
            eprom_offset=payload,
            header_offset=offset,
            space="P",
            load_address=0x0000,
            unit="word24",
            count=count,
            size=count * WORD,
            sha256=digest(image[payload : payload + count * WORD]),
            notes="DSP bootstrap loaded at P:$0000; header is a 2-byte word count",
            programs="dsp_boot",
        )
    )
    offset = payload + count * WORD

    for index, (header, load, words, number) in enumerate(EXPECTED_IMAGES, start=2):
        if offset != header:
            raise SystemExit(
                f"load image {index}: header at {offset:#06x}, the known layout "
                f"says {header:#06x}"
            )
        got_load = int.from_bytes(image[offset : offset + 2], "big")
        got_words = int.from_bytes(image[offset + 2 : offset + 4], "big")
        if (got_load, got_words) != (load, words):
            raise SystemExit(
                f"load image at {offset:#06x} says P:${got_load:04x}/{got_words}w, "
                f"the known layout says P:${load:04x}/{words}w"
            )
        payload = offset + 4
        if number is None:
            name = f"{index:02d}_dsp_shared_p{load:04x}_{words}w.bin"
            region, notes, programs = (
                "dsp_shared",
                "shared DSP code loaded at P:$0400 by every configuration",
                DSP_CFG_PROGRAMS,
            )
        else:
            name = f"{index:02d}_dsp_cfg{number}_p{load:04x}_{words}w.bin"
            region = f"dsp_cfg{number}"
            notes = f"configuration {number}: {CONFIG_NAMES[number]}"
            programs = region
        records.append(
            Record(
                filename=name,
                region=region,
                processor=DSP,
                eprom_offset=payload,
                header_offset=offset,
                space="P",
                load_address=load,
                unit="word24",
                count=words,
                size=words * WORD,
                sha256=digest(image[payload : payload + words * WORD]),
                notes=notes,
                programs=programs,
            )
        )
        offset = payload + words * WORD

    if offset != FILL_START:
        raise SystemExit(
            f"load images end at {offset:#06x}, expected {FILL_START:#06x}"
        )
    fill = image[offset:]
    if len(fill) != EPROM_SIZE - offset or set(fill) != {0xFF}:
        raise SystemExit(
            f"{EPROM_SIZE - offset} bytes at {offset:#06x} are not an all-0xFF fill"
        )

    unknown = {record.filename for record in records} - set(RECORD_SHA256)
    if unknown:
        raise SystemExit(f"no expected SHA-256 for {sorted(unknown)}")
    for record in records:
        wanted = RECORD_SHA256[record.filename]
        if record.sha256 != wanted:
            raise SystemExit(
                f"{record.filename}: sha256 {record.sha256}, expected {wanted or '?'}"
            )
    return records


def write_records(outdir: Path, image: bytes, records: list[Record]) -> Path:
    firmware = outdir / "firmware"
    firmware.mkdir(parents=True, exist_ok=True)
    for record in records:
        (firmware / record.filename).write_bytes(
            image[record.eprom_offset : record.end + 1]
        )
    return firmware


def write_manifest(outdir: Path, records: list[Record]) -> None:
    with (outdir / "firmware" / "manifest.csv").open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(
            (
                "filename",
                "region",
                "processor",
                "eprom_offset",
                "header_offset",
                "space",
                "load_address",
                "unit",
                "count",
                "bytes",
                "sha256",
                "notes",
            )
        )
        for record in records:
            writer.writerow(
                (
                    record.filename,
                    record.region,
                    record.processor,
                    f"0x{record.eprom_offset:04x}",
                    ""
                    if record.header_offset is None
                    else f"0x{record.header_offset:04x}",
                    record.space,
                    f"0x{record.load_address:04x}",
                    record.unit,
                    record.count,
                    record.size,
                    record.sha256,
                    record.notes,
                )
            )


def write_memory_map(outdir: Path, records: list[Record]) -> None:
    rows = []
    for record in records:
        if record.header_offset is not None:
            rows.append(
                (
                    record.region,
                    record.processor,
                    record.programs,
                    "header",
                    f"0x{record.header_offset:04x}",
                    "",
                    record.eprom_offset - record.header_offset,
                    "byte",
                    "",
                    f"header of {record.filename}",
                )
            )
        rows.append(
            (
                record.region,
                record.processor,
                record.programs,
                "code",
                f"0x{record.eprom_offset:04x}",
                f"0x{record.load_address:04x}",
                record.size,
                record.unit,
                record.filename,
                record.notes,
            )
        )
    rows.sort(key=lambda row: (int(row[4], 16), row[3]))
    rows.append(
        (
            "fill",
            "",
            "",
            "fill",
            f"0x{FILL_START:04x}",
            "",
            EPROM_SIZE - FILL_START,
            "byte",
            "",
            "all 0xFF, the rest of the 32 KiB EPROM",
        )
    )
    with (outdir / "memory_map.csv").open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(
            (
                "region",
                "processor",
                "programs",
                "kind",
                "eprom_offset",
                "load_address",
                "size",
                "unit",
                "file",
                "notes",
            )
        )
        writer.writerows(rows)


def resolve_disassembler(value: str) -> str:
    if "/" in value:
        path = Path(value).expanduser()
        if not path.is_file():
            raise SystemExit(f"disassembler not found: {path}")
        return str(path.resolve())
    found = shutil.which(value)
    if found is None:
        raise SystemExit(
            f"disassembler {value!r} not on PATH: install dsp56300-disasm from "
            "https://github.com/xemu-project/dsp56300 or pass --disassembler "
            "(DISASSEMBLER= for make)"
        )
    return found


def disassemble(exe: str, path: Path, start: int, count: int) -> str:
    done = subprocess.run(
        [
            exe,
            "-f",
            "u24be",
            "--color",
            "never",
            "-s",
            f"{start:x}",
            "-n",
            str(count),
            str(path),
        ],
        capture_output=True,
        text=True,
        timeout=DISASSEMBLER_TIMEOUT,
        check=False,
    )
    if done.returncode != 0:
        raise SystemExit(f"disassembler failed on {path.name}: {done.stderr}")
    if not done.stdout.strip():
        raise SystemExit(f"disassembler produced no output for {path.name}")
    return done.stdout


def parse_dsp(text: str) -> list[DspInsn]:
    instructions = []
    for line in text.splitlines():
        match = DSP_INSTRUCTION.match(line)
        if match is None:
            continue
        tokens = match.group(2).split()
        if len(tokens) > 1 and DSP_OPERAND_WORD.match(tokens[0]):
            tokens = tokens[1:]
        operand = " ".join(tokens[1:])
        found = DSP_TARGET.search(operand)
        instructions.append(
            DspInsn(
                address=int(match.group(1), 16),
                mnemonic=tokens[0].lower(),
                operand=operand,
                target=int(found.group(1), 16) if found else None,
            )
        )
    return instructions


def dsp_kind(mnemonic: str) -> str | None:
    if not mnemonic.startswith("j"):
        return None
    return "call" if mnemonic.startswith("js") else "branch"


def write_dsp_asm(
    exe: str, firmware: Path, outdir: Path, records: list[Record]
) -> dict[str, list[DspInsn]]:
    asm = outdir / "asm"
    asm.mkdir(parents=True, exist_ok=True)
    by_region = {record.region: record for record in records if record.processor == DSP}
    parsed: dict[str, list[DspInsn]] = {}

    def run(record: Record, path: Path, count: int | None = None) -> str:
        return disassemble(
            exe, path, record.load_address, record.count if count is None else count
        )

    boot = by_region["dsp_boot"]
    shared = by_region["dsp_shared"]
    boot_text = run(boot, firmware / boot.filename)
    shared_text = run(shared, firmware / shared.filename)
    parsed["dsp_boot"] = parse_dsp(boot_text)
    parsed["dsp_shared"] = parse_dsp(shared_text)
    (asm / "dsp_boot.asm").write_text(
        "; dsp_boot: DSP bootstrap P:$0000..$00D2, loaded by the 8051 host port\n"
        + boot_text
    )
    (asm / "dsp_shared.asm").write_text(
        "; dsp_shared: shared DSP code P:$0400..$0A0F, loaded by every configuration\n"
        + shared_text
    )

    with tempfile.TemporaryDirectory() as tmp:
        init_words = DSP_CFG_LOAD
        slice_path = Path(tmp) / "boot_init.bin"
        slice_path.write_bytes(
            (firmware / boot.filename).read_bytes()[: init_words * WORD]
        )
        boot_init = disassemble(exe, slice_path, 0, init_words)
    parsed["dsp_cfg_init"] = parse_dsp(boot_init)

    for index in range(6):
        record = by_region[f"dsp_cfg{index}"]
        text = run(record, firmware / record.filename)
        parsed[record.region] = parse_dsp(text)
        end = record.load_address + record.count - 1
        header = (
            f"; dsp_cfg{index} program, configuration {index}: {CONFIG_NAMES[index]}\n"
            f"; P:$0000..$009F boot (its $00A0..$00D2 setup is overwritten),\n"
            f"; P:${record.load_address:04X}..${end:04X} configuration, "
            f"P:${DSP_SHARED_RANGE[0]:04X}..${DSP_SHARED_RANGE[1]:04X} shared\n"
        )
        (asm / f"dsp_cfg{index}.asm").write_text(
            header + boot_init + "\n" + text + "\n" + shared_text
        )
    return parsed


def write_mcu_asm(outdir: Path, image: bytes, analysis: mcs51_disasm.Analysis) -> None:
    (outdir / "asm").mkdir(parents=True, exist_ok=True)
    header = (
        f"; mcu: 8051 CODE:0000..{MCU_END:04X}, the whole EPROM is the code space.\n"
        "; Undecoded bytes are data (preset tables, strings, jump tables).\n"
    )
    (outdir / "asm" / "mcu.asm").write_text(
        header + mcs51_disasm.render(image, analysis, MCU_START, MCU_END)
    )


def record_span(record: Record) -> tuple[int, int]:
    return record.load_address, record.load_address + record.count - 1


def dsp_resolution(target: int, own: Record, records: list[Record]) -> str:
    low, high = record_span(own)
    if low <= target <= high:
        return own.region
    if DSP_SHARED_RANGE[0] <= target <= DSP_SHARED_RANGE[1]:
        return "dsp_shared"
    if target < DSP_CFG_LOAD:
        return "dsp_boot"
    if DSP_CFG_RANGE[0] <= target <= DSP_CFG_RANGE[1] and own.region in (
        "dsp_boot",
        "dsp_shared",
    ):
        return "configuration"
    return "external"


def build_calls(
    mcu: mcs51_disasm.Analysis, dsp: dict[str, list[DspInsn]], records: list[Record]
) -> list[Call]:
    calls = []
    for instruction in sorted(mcu.instructions.values()):
        if not instruction.is_call:
            continue
        target = instruction.target
        resolution = "mcu" if MCU_START <= target <= MCU_END else "external"
        calls.append(
            Call(
                MCU,
                "mcu",
                instruction.address,
                instruction.mnemonic,
                target,
                resolution,
            )
        )
    by_region = {record.region: record for record in records if record.processor == DSP}
    for region, record in by_region.items():
        for instruction in dsp[region]:
            if dsp_kind(instruction.mnemonic) != "call" or instruction.target is None:
                continue
            calls.append(
                Call(
                    DSP,
                    region,
                    instruction.address,
                    instruction.mnemonic,
                    instruction.target,
                    dsp_resolution(instruction.target, record, records),
                )
            )
    return calls


def build_entries(boot: bytes, dsp: dict[str, list[DspInsn]]) -> list[Entry]:
    entries = [
        Entry(name, address, MCU, MCU_PROGRAM, MCU_ENTRY_EVIDENCE[name])
        for name, address in MCU_ENTRY_POINTS
    ]
    for vector in DSP_VECTORS:
        entries.append(
            Entry(
                vector.name,
                vector.slot,
                DSP,
                programs_for(DSP, vector.slot),
                f"vector slot P:${vector.slot:04X}: {describe_slot(boot, vector, dsp)}",
            )
        )
    for name, address in DSP_ENTRY_POINTS:
        entries.append(
            Entry(
                name,
                address,
                DSP,
                programs_for(DSP, address),
                DSP_ENTRY_EVIDENCE[name],
            )
        )
    return entries


def describe_slot(boot: bytes, vector: Vector, dsp: dict[str, list[DspInsn]]) -> str:
    if vector.kind in ("jmp", "jsr"):
        return f"{vector.kind} ${vector.target:04x}"
    body = [
        f"{insn.mnemonic} {insn.operand}".strip()
        for insn in dsp["dsp_boot"]
        if vector.slot <= insn.address < vector.slot + 2
    ]
    first = int.from_bytes(boot[vector.slot * WORD : vector.slot * WORD + 3], "big")
    return f"fast interrupt (2 words, ${first:06x}...): {' ; '.join(body)}"


def check_mcu_vectors(image: bytes, analysis: mcs51_disasm.Analysis) -> None:
    reset = analysis.instructions.get(0x0000)
    if reset is None or reset.mnemonic != "ljmp" or reset.target != 0x0026:
        raise SystemExit("reset vector 0x0000 is not `ljmp 0x0026`")
    timer = analysis.instructions.get(0x000B)
    follow = analysis.instructions.get(0x000D)
    if timer is None or timer.mnemonic != "clr" or timer.operands[0] != "TR0":
        raise SystemExit("timer-0 vector 0x000B is not `clr TR0`")
    if follow is None or follow.mnemonic != "ljmp" or follow.target != 0x14E3:
        raise SystemExit("timer-0 vector does not continue with `ljmp 0x14E3`")
    serial = analysis.instructions.get(0x0023)
    if serial is None or serial.mnemonic != "ljmp" or serial.target != 0x1A3A:
        raise SystemExit("serial vector 0x0023 is not `ljmp 0x1A3A`")
    for vector in MCU_UNUSED_VECTORS:
        if image[vector : vector + 2] != b"\xff\xff":
            raise SystemExit(f"unused vector {vector:#06x} does not hold 0xFFFF")


def check_dsp_vectors(image: bytes, dsp: dict[str, list[DspInsn]]) -> None:
    def words(slot: int) -> tuple[int, int]:
        base = slot * WORD
        return (
            int.from_bytes(image[base : base + 3], "big"),
            int.from_bytes(image[base + 3 : base + 6], "big"),
        )

    for vector in DSP_VECTORS:
        first, second = words(vector.slot)
        if vector.kind == "jmp" and (first, second) != (DSP_JMP, vector.target):
            raise SystemExit(
                f"DSP slot ${vector.slot:04X} is {first:06x}/{second:06x}, "
                f"not `jmp ${vector.target:04x}`"
            )
        if vector.kind == "jsr" and (first, second) != (DSP_JSR, vector.target):
            raise SystemExit(
                f"DSP slot ${vector.slot:04X} is {first:06x}/{second:06x}, "
                f"not `jsr ${vector.target:04x}`"
            )
        if vector.kind == "fast" and first == 0 and second == 0:
            raise SystemExit(f"DSP slot ${vector.slot:04X} is empty, not a handler")
    for slot in DSP_UNUSED_VECTORS:
        if words(slot) != (0, 0):
            raise SystemExit(f"unused DSP slot ${slot:04X} is not 0/0")

    found = []
    for entry, site, target, mnemonic in DSP_CROSS_ENTRIES:
        region = "dsp_boot" if site < DSP_CFG_LOAD else "dsp_shared"
        for instruction in dsp[region]:
            if instruction.address != site:
                continue
            if instruction.mnemonic != mnemonic or instruction.target != target:
                raise SystemExit(
                    f"${site:04X} is `{instruction.mnemonic} {instruction.operand}`, "
                    f"not `{mnemonic} ${target:04x}`"
                )
            found.append(entry)
    missing = {entry for entry, *_ in DSP_CROSS_ENTRIES} - set(found)
    if missing:
        raise SystemExit(f"cross-image entry checks missing: {sorted(missing)}")


def build_seeds(
    mcu: mcs51_disasm.Analysis,
    dsp: dict[str, list[DspInsn]],
    entries: list[Entry],
    records: list[Record],
) -> dict[tuple[str, int], list[str]]:
    seeds: dict[tuple[str, int], list[str]] = {}

    def add(processor: str, address: int, source: str) -> None:
        sources = seeds.setdefault((processor, address), [])
        if source not in sources:
            sources.append(source)

    for entry in entries:
        add(entry.processor, entry.address, entry.name)
    for record in records:
        add(record.processor, record.load_address, f"record_start:{record.region}")
    for instruction in mcu.instructions.values():
        if instruction.is_branch and instruction.target is not None:
            add(
                MCU,
                instruction.target,
                f"{instruction.mnemonic}_from_{instruction.address:04X}",
            )
    for table in mcu.tables:
        for target in table.targets:
            add(MCU, target, f"jump_table_at_{table.site:04X}")
    for region, instructions in dsp.items():
        if region == "dsp_cfg_init":
            continue
        for instruction in instructions:
            kind = dsp_kind(instruction.mnemonic)
            if kind is None or instruction.target is None:
                continue
            add(
                DSP,
                instruction.target,
                f"{instruction.mnemonic}_from_{instruction.address:06x}",
            )
    return seeds


def join_sources(sources: list[str]) -> str:
    # Hot targets can have dozens of call sites; the full list is in direct_calls.csv.
    if len(sources) <= 3:
        return ";".join(sources)
    return ";".join(sources[:3]) + f";+{len(sources) - 3}_more"


def write_seeds(outdir: Path, seeds: dict[tuple[str, int], list[str]]) -> None:
    with (outdir / "seeds.csv").open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(("address", "programs", "source"))
        for processor, address in sorted(seeds, key=lambda key: (key[0], key[1])):
            writer.writerow(
                (
                    hex_address(address, processor),
                    programs_for(processor, address),
                    join_sources(seeds[(processor, address)]),
                )
            )


def write_entrypoints(outdir: Path, entries: list[Entry]) -> None:
    with (outdir / "entrypoints.csv").open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(("name", "address", "programs", "evidence"))
        for entry in entries:
            writer.writerow(
                (
                    entry.name,
                    hex_address(entry.address, entry.processor),
                    entry.programs,
                    entry.evidence,
                )
            )


def write_callgraph(outdir: Path, calls: list[Call], entries: list[Entry]) -> None:
    graph = outdir / "callgraph"
    graph.mkdir(parents=True, exist_ok=True)

    with (graph / "direct_calls.csv").open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(
            ("processor", "record", "site", "mnemonic", "target", "resolution")
        )
        for call in calls:
            writer.writerow(
                (
                    call.processor,
                    call.record,
                    hex_address(call.site, call.processor),
                    call.mnemonic,
                    hex_address(call.target, call.processor),
                    call.resolution,
                )
            )

    callers: dict[tuple[str, int], list[int]] = {}
    for call in calls:
        callers.setdefault((call.processor, call.target), []).append(call.site)

    with (graph / "functions.csv").open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(("processor", "target", "callers", "first_caller"))
        for processor, target in sorted(callers):
            sites = callers[(processor, target)]
            writer.writerow(
                (
                    processor,
                    hex_address(target, processor),
                    len(sites),
                    hex_address(min(sites), processor),
                )
            )

    labels: dict[tuple[str, int], list[str]] = {}
    for entry in entries:
        labels.setdefault((entry.processor, entry.address), []).append(entry.name)
    nodes = set(labels)
    for call in calls:
        nodes.add((call.processor, call.site))
        nodes.add((call.processor, call.target))
    external = {
        (call.processor, call.target)
        for call in calls
        if call.resolution in ("external", "configuration")
    }

    lines = [
        "digraph direct_calls {",
        '  rankdir="LR";',
        '  node [shape="box", fontname="monospace", fontsize=9];',
    ]
    for processor, address in sorted(nodes):
        text = f"{processor}:{hex_address(address, processor)}"
        if (processor, address) in labels:
            text += ":" + ",".join(labels[(processor, address)])
        attrs = ['label="' + text.replace('"', "'") + '"']
        if (processor, address) in external:
            attrs += ['style="dashed"', 'color="gray40"', 'fontcolor="gray40"']
        lines.append(f'  "{text}" [{", ".join(attrs)}];')
    for call in calls:
        source = f"{call.processor}:{hex_address(call.site, call.processor)}"
        target = f"{call.processor}:{hex_address(call.target, call.processor)}"
        lines.append(f'  "{source}" -> "{target}" [label="{call.mnemonic}"];')
    lines.append("}")
    (graph / "direct_calls.dot").write_text("\n".join(lines) + "\n")


def report(
    records: list[Record],
    mcu: mcs51_disasm.Analysis,
    dsp: dict[str, list[DspInsn]],
    calls: list[Call],
    seeds: dict[tuple[str, int], list[str]],
    entries: list[Entry],
) -> None:
    print("records:")
    for record in records:
        print(
            f"  {record.filename:34s} {record.region:11s} "
            f"0x{record.eprom_offset:04x} {record.count:6d}{record.unit[:1]} "
            f"{record.sha256}"
        )
    code = sum(instruction.length for instruction in mcu.instructions.values())
    print(
        f"8051: {len(mcu.instructions)} instructions, {code} of "
        f"{MCU_END - MCU_START + 1} bytes decoded"
    )
    for table in sorted(mcu.tables, key=lambda table: table.site):
        print(
            f"  jump table at C:{table.address:04X} from C:{table.site:04X}: "
            f"{table.entries} entries of {table.entry_size} bytes"
        )
    if mcu.unresolved:
        sites = " ".join(f"C:{site:04X}" for site in mcu.unresolved)
        print(f"  unresolved JMP @A+DPTR: {sites}")
    else:
        print("  unresolved JMP @A+DPTR: none")
    for region in [record.region for record in records if record.processor == DSP]:
        print(f"DSP {region}: {len(dsp[region])} instructions")
    for processor in (MCU, DSP):
        count = sum(1 for call in calls if call.processor == processor)
        print(f"direct calls from {processor}: {count}")
    print(f"seed addresses: {len(seeds)}")
    print(f"entry points: {len(entries)}")


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    ap.add_argument("--disassembler", default=DEFAULT_DISASSEMBLER)
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    exe = resolve_disassembler(args.disassembler)

    image = read_eprom()
    report_manual()
    records = split(image)
    firmware = write_records(args.output, image, records)
    write_manifest(args.output, records)

    mcu = mcs51_disasm.descend(
        image, tuple(address for _, address in MCU_ENTRY_POINTS), MCU_END
    )
    dsp = write_dsp_asm(exe, firmware, args.output, records)
    write_mcu_asm(args.output, image, mcu)

    boot = next(record for record in records if record.region == "dsp_boot")
    boot_bytes = (firmware / boot.filename).read_bytes()
    check_mcu_vectors(image, mcu)
    check_dsp_vectors(boot_bytes, dsp)

    calls = build_calls(mcu, dsp, records)
    entries = build_entries(boot_bytes, dsp)
    seeds = build_seeds(mcu, dsp, entries, records)

    write_memory_map(args.output, records)
    write_entrypoints(args.output, entries)
    write_callgraph(args.output, calls, entries)
    write_seeds(args.output, seeds)

    report(records, mcu, dsp, calls, seeds, entries)
    print(
        "checks: layout matches the known table, records match their SHA-256, "
        "pieces tile the EPROM, vectors and cross-image entries hold as stated"
    )
    print(f"written to {args.output}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
