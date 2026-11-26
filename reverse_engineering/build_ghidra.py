#!/usr/bin/env python3
"""Build the Intellifex LTD Ghidra project from the firmware records with PyGhidra.

Imports the 8051 program and the DSP bootstrap as standalone programs, and each
DSP configuration as the address image it runs: the boot words the configuration
leaves in place, the configuration at P:$00A0 and the shared code at P:$0400. Seeds
disassembly from `seeds.csv`, creates entry points from `entrypoints.csv`, runs
Ghidra analysis and exports the function and call tables to `ghidra_analysis/`.
Standalone PyGhidra only: no Java scripts and no analyzeHeadless.

With `--prepare DIR` it only writes the same programs' images and a list of how to
import them by hand in the Ghidra GUI, and needs no PyGhidra.
"""

import argparse
import csv
import sys
import time
from pathlib import Path
from typing import NamedTuple

RE_ROOT = Path(__file__).resolve().parent
# reverse_engineering/ghidra is the project directory, and PyGhidra only drops the
# working directory from sys.path. With this file's directory on the path, the import
# system resolves `ghidra` to that directory and startup dies on import recursion.
for _entry in list(sys.path):
    if Path(_entry or ".").resolve() == RE_ROOT:
        sys.path.remove(_entry)

PROJECT_DIR = RE_ROOT / "ghidra"
ANALYSIS_DIR = RE_ROOT / "ghidra_analysis"
SEEDS = RE_ROOT / "seeds.csv"
ENTRYPOINTS = RE_ROOT / "entrypoints.csv"
DEFAULT_FIRMWARE = RE_ROOT / "firmware"

WORD = 3
# The DSP56000 language holds a word in a 4-byte big-endian unit with the high byte zero.
UNIT = 4
MCU_LANGUAGE = "8051:BE:16:default"
DSP_LANGUAGE = "DSP56000:BE:16:default"
LOADER = "BinaryLoader"
JDK_OPTIONS = ("--enable-final-field-mutation=ALL-UNNAMED",)
DEFAULT_PROJECT = "Intellifex"
DEFAULT_GHIDRA = Path("/opt/ghidra")
DEFAULT_ANALYSIS_TIMEOUT = 900

BOOT_REGION = "dsp_boot"
SHARED_REGION = "dsp_shared"
CONFIGURATION_ENTRY = 0x00A0

# Byte addresses of interest per processor, printed in _info.txt with their instruction.
MCU_KEY_ADDRESSES = (0x0000, 0x000B, 0x0023, 0x0026, 0x0F2F, 0x0F37, 0x0F48, 0x1A3A)
DSP_KEY_ADDRESSES = (0x0000, 0x0024, 0x0038, 0x003C, 0x0097, 0x00A0, 0x0400, 0x0447)


class Record(NamedTuple):
    filename: str
    region: str
    processor: str
    load_address: int
    bytes: int

    @property
    def unit(self) -> int:
        return UNIT if self.processor != "8051" else 1


class Region(NamedTuple):
    name: str
    record: str
    address: int


class Spec(NamedTuple):
    stage: str
    language: str
    unit: int
    record: str
    initial_block: str
    boot_prefix: int
    regions: tuple[Region, ...]
    cross_target: int | None

    @property
    def name(self) -> str:
        return f"ifx_{self.stage}"

    @property
    def import_name(self) -> str:
        return f"{self.name}.be32.bin" if self.unit == UNIT else f"{self.name}.bin"

    @property
    def project_path(self) -> str:
        return "/" + self.import_name


def read_manifest(firmware: Path) -> dict[str, Record]:
    """Region -> record, from the manifest build_workspace.py wrote."""
    manifest = firmware / "manifest.csv"
    if not manifest.is_file():
        raise SystemExit(f"missing {manifest}; run make ghidra-prepare")
    records = {}
    with manifest.open(newline="") as fh:
        for row in csv.DictReader(fh):
            records[row["region"]] = Record(
                filename=row["filename"],
                region=row["region"],
                processor=row["processor"],
                load_address=int(row["load_address"], 16),
                bytes=int(row["bytes"]),
            )
    return records


def read_record(firmware: Path, record: Record) -> bytes:
    """One firmware record, its 24-bit words widened to the language's 4-byte units."""
    path = firmware / record.filename
    if not path.is_file():
        raise SystemExit(f"missing firmware record {path}")
    blob = path.read_bytes()
    if len(blob) != record.bytes:
        raise SystemExit(
            f"{record.filename}: {len(blob)} bytes, manifest {record.bytes}"
        )
    if record.unit == 1:
        return blob
    if len(blob) % WORD:
        raise SystemExit(f"{record.filename}: {len(blob)} bytes is not whole words")
    return b"".join(b"\0" + blob[i : i + WORD] for i in range(0, len(blob), WORD))


def build_specs(records: dict[str, Record]) -> tuple[Spec, ...]:
    mcu = records["mcu"]
    boot = records[BOOT_REGION]
    shared = records[SHARED_REGION]
    specs = [
        Spec(
            stage="mcu",
            language=MCU_LANGUAGE,
            unit=1,
            record=mcu.filename,
            initial_block="code",
            boot_prefix=0,
            regions=(),
            cross_target=None,
        ),
        Spec(
            stage=BOOT_REGION,
            language=DSP_LANGUAGE,
            unit=UNIT,
            record=boot.filename,
            initial_block="boot",
            boot_prefix=0,
            regions=(),
            cross_target=None,
        ),
    ]
    for index in range(6):
        configuration = records[f"dsp_cfg{index}"]
        specs.append(
            Spec(
                stage=configuration.region,
                language=DSP_LANGUAGE,
                unit=UNIT,
                record=configuration.filename,
                initial_block="boot_and_configuration",
                # The configuration overwrites the bootstrap from its load address on.
                boot_prefix=configuration.load_address,
                regions=(Region("shared", shared.filename, shared.load_address),),
                cross_target=CONFIGURATION_ENTRY,
            )
        )
    return tuple(specs)


def program_image(
    spec: Spec, records: dict[str, Record], blobs: dict[str, bytes]
) -> bytes:
    data = blobs[spec.record]
    if not spec.boot_prefix:
        return data
    return blobs[records[BOOT_REGION].filename][: spec.boot_prefix * UNIT] + data


def read_seeds(stages: tuple[str, ...]) -> dict[str, list[int]]:
    """Workspace addresses per program, in file order and without repeats."""
    seeds: dict[str, list[int]] = {stage: [] for stage in stages}
    seen: dict[str, set[int]] = {stage: set() for stage in stages}
    with SEEDS.open(newline="") as fh:
        rows = csv.reader(fh)
        next(rows, None)
        for row in rows:
            if len(row) < 2:
                continue
            address = int(row[0], 16)
            for stage in row[1].split(";"):
                if stage in seeds and address not in seen[stage]:
                    seen[stage].add(address)
                    seeds[stage].append(address)
    return seeds


def read_entrypoints() -> dict[str, list[tuple[str, int]]]:
    """(name, workspace address) per program, in file order."""
    entries: dict[str, list[tuple[str, int]]] = {}
    with ENTRYPOINTS.open(newline="") as fh:
        for row in csv.DictReader(fh):
            for stage in row["programs"].split(";"):
                entries.setdefault(stage, []).append(
                    (row["name"], int(row["address"], 16))
                )
    return entries


def program_address(program, value: int):
    """The address of a workspace address, a byte for the 8051 and a P word for the DSP."""
    return (
        program.getAddressFactory()
        .getDefaultAddressSpace()
        .getTruncatedAddress(value, True)
    )


def setup_program(
    program, spec: Spec, blobs: dict[str, bytes], seeds: list[int], entries
) -> int:
    import jpype
    import pyghidra
    from ghidra.program.flatapi import FlatProgramAPI
    from ghidra.program.model.symbol import SourceType
    from java.io import ByteArrayInputStream

    def at(value: int):
        return program_address(program, value)

    memory = program.getMemory()
    listing = program.getListing()
    monitor = pyghidra.task_monitor()

    with pyghidra.transaction(program, f"Set up {spec.name}"):
        program.setName(spec.name)

        blocks = memory.getBlocks()
        if not blocks:
            raise SystemExit(f"{spec.name}: the import produced no memory block")
        initial = blocks[0]
        initial.setName(spec.initial_block)
        initial.setExecute(True)
        initial.setComment(f"Intellifex LTD {spec.stage} image")

        if spec.unit == UNIT:
            # The language's default X and Y blocks end at $3FFF: their length counts
            # bytes, not words.
            factory = program.getAddressFactory()
            for space_name in ("x", "y"):
                space = factory.getAddressSpace(space_name)
                for block in list(memory.getBlocks()):
                    if block.getStart().getAddressSpace() == space:
                        memory.removeBlock(block, monitor)
                memory.createUninitializedBlock(
                    space_name.upper(),
                    space.getTruncatedAddress(0, True),
                    0x10000 * UNIT,
                    False,
                ).setWrite(True)

        api = FlatProgramAPI(program)
        for region in spec.regions:
            data = blobs[region.record]
            words = jpype.JArray(jpype.JByte)(data)
            block = memory.createInitializedBlock(
                region.name,
                at(region.address),
                ByteArrayInputStream(words),
                len(data),
                monitor,
                False,
            )
            block.setExecute(True)
            block.setRead(True)
            block.setWrite(True)
            block.setComment(f"Intellifex LTD {region.name} region")
            api.createLabel(
                at(region.address), region.name, True, SourceType.USER_DEFINED
            )
            api.disassemble(at(region.address))

        for address in seeds:
            api.disassemble(at(address))

        for name, address in entries:
            point = at(address)
            api.createLabel(point, name, True, SourceType.USER_DEFINED)
            api.addEntryPoint(point)
            api.disassemble(point)
            if listing.getInstructionAt(point) is not None:
                api.createFunction(point, name)

    return len(seeds)


def analyze_program(program, timeout: int) -> float:
    import pyghidra

    started = time.monotonic()
    pyghidra.analyze(program, pyghidra.task_monitor(timeout))
    elapsed = time.monotonic() - started
    if elapsed >= timeout:
        raise SystemExit(
            f"{program.getName()}: analysis hit the {timeout}s timeout and is "
            "incomplete; raise --analysis-timeout and rebuild"
        )
    return elapsed


def export_program(program, spec: Spec, outdir: Path) -> Path:
    import pyghidra

    name = program.getName()
    monitor = pyghidra.task_monitor()
    listing = program.getListing()
    functions = program.getFunctionManager()
    edges = [
        (
            function,
            # Ghidra returns callees from an identity-hashed HashSet, so sort for a
            # reproducible export.
            sorted(
                function.getCalledFunctions(monitor),
                key=lambda target: target.getEntryPoint().getOffset(),
            ),
        )
        for function in functions.getFunctions(True)
    ]

    functions_path = outdir / f"{name}_functions.csv"
    with functions_path.open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(("address", "name", "body_bytes", "callers", "callees"))
        for function, callees in edges:
            writer.writerow(
                (
                    function.getEntryPoint(),
                    function.getName(),
                    function.getBody().getNumAddresses(),
                    len(function.getCallingFunctions(monitor)),
                    len(callees),
                )
            )

    calls_path = outdir / f"{name}_calls.csv"
    with calls_path.open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(("caller", "caller_name", "target", "target_name"))
        for function, callees in edges:
            for target in callees:
                writer.writerow(
                    (
                        function.getEntryPoint(),
                        function.getName(),
                        target.getEntryPoint(),
                        target.getName(),
                    )
                )

    keys = DSP_KEY_ADDRESSES if spec.unit == UNIT else MCU_KEY_ADDRESSES
    lines = [
        f"program={name}",
        f"language={program.getLanguageID()}",
        f"domain_file={program.getDomainFile().getName()}",
        f"instructions={listing.getNumInstructions()}",
        f"functions={functions.getFunctionCount()}",
        f"address_unit_bytes={spec.unit}",
    ]
    for block in program.getMemory().getBlocks():
        execute = "true" if block.isExecute() else "false"
        lines.append(
            f"block={block.getName()},{block.getStart()},{block.getEnd()},"
            f"execute={execute}"
        )
    for value in keys:
        address = program_address(program, value)
        instruction = listing.getInstructionAt(address)
        if instruction is not None:
            lines.append(f"instruction={address},{instruction}")
    if spec.cross_target is not None:
        target = program_address(program, spec.cross_target)
        lines.append(f"references_to_{target}:")
        for reference in program.getReferenceManager().getReferencesTo(target):
            lines.append(
                f"  {reference.getFromAddress()} {reference.getReferenceType()}"
            )

    info_path = outdir / f"{name}_info.txt"
    info_path.write_text("\n".join(lines) + "\n")
    return info_path


def import_program(project, spec: Spec, data: bytes) -> None:
    import jpype
    import pyghidra

    results = (
        pyghidra.program_loader()
        .project(project)
        .source(jpype.JArray(jpype.JByte)(data))
        .name(spec.import_name)
        .loaders(LOADER)
        .language(spec.language)
        .load()
    )
    results.save(pyghidra.task_monitor())


def report(program, spec: Spec, seed_count: int, elapsed: float) -> None:
    print(
        f"{spec.name}: blocks={len(program.getMemory().getBlocks())} "
        f"instructions={program.getListing().getNumInstructions()} "
        f"functions={program.getFunctionManager().getFunctionCount()} "
        f"seeds={seed_count} analysis={elapsed:.1f}s"
    )


IMPORT_GUIDE = f"""\
Intellifex LTD records, for importing by hand in the Ghidra GUI.

Import each program's first file with File > Import File: format Raw Binary,
language 8051:BE:16:default (mcu) or {DSP_LANGUAGE} (the DSP programs),
base address 0. Add a program's other files with File > Add To Program, each at
its address under its block name. The DSP images hold each 24-bit word in a
4-byte big-endian unit, and their addresses are P words, as in the CSVs. The six
configurations overwrite the bootstrap from P:$00A0, so they are imported as the
boot prefix plus the configuration plus the shared code. build_ghidra.py builds
the same programs with seeds, entry points and analysis.

"""


def prepare(firmware: Path, outdir: Path) -> int:
    records = read_manifest(firmware)
    specs = build_specs(records)
    blobs = {
        record.filename: read_record(firmware, record) for record in records.values()
    }
    outdir.mkdir(parents=True, exist_ok=True)
    rows = []
    for spec in specs:
        files = [
            (
                spec.import_name,
                program_image(spec, records, blobs),
                spec.initial_block,
                0,
            )
        ]
        files += [
            (
                f"{spec.name}_{region.name}{Path(region.record).suffix}",
                blobs[region.record],
                region.name,
                region.address,
            )
            for region in spec.regions
        ]
        for name, data, block, address in files:
            (outdir / name).write_bytes(data)
            rows.append(f"{spec.name:<14} {name:<30} {block:<24} 0x{address:04X}")
    header = f"{'program':<14} {'file':<30} {'block':<24} address"
    (outdir / "IMPORT.txt").write_text(IMPORT_GUIDE + "\n".join([header, *rows]) + "\n")
    print(f"{len(rows)} images and IMPORT.txt written to {outdir}")
    return 0


def parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("project", nargs="?", default=DEFAULT_PROJECT)
    ap.add_argument("--ghidra", type=Path, default=DEFAULT_GHIDRA)
    ap.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE)
    ap.add_argument("--analysis-timeout", type=int, default=DEFAULT_ANALYSIS_TIMEOUT)
    ap.add_argument(
        "--prepare",
        type=Path,
        metavar="DIR",
        help="write the program images and an import list to DIR for the Ghidra GUI, and stop",
    )
    return ap.parse_args()


def main() -> int:
    args = parse_args()
    if args.prepare is not None:
        return prepare(args.firmware, args.prepare)

    import pyghidra

    records = read_manifest(args.firmware)
    specs = build_specs(records)
    blobs = {
        record.filename: read_record(args.firmware, record)
        for record in records.values()
    }
    gpr = PROJECT_DIR / f"{args.project}.gpr"
    rep = PROJECT_DIR / f"{args.project}.rep"
    if gpr.exists() or rep.exists():
        raise SystemExit(
            f"refusing to overwrite the existing project {args.project} in "
            f"{PROJECT_DIR}; pick another name"
        )
    if not args.ghidra.is_dir():
        raise SystemExit(f"Ghidra install directory not found: {args.ghidra}")

    PROJECT_DIR.mkdir(parents=True, exist_ok=True)
    ANALYSIS_DIR.mkdir(parents=True, exist_ok=True)

    launcher = pyghidra.HeadlessPyGhidraLauncher(install_dir=args.ghidra)
    launcher.add_vmargs(*JDK_OPTIONS)
    launcher.start()

    seeds = read_seeds(tuple(spec.stage for spec in specs))
    entries = read_entrypoints()

    project = pyghidra.open_project(PROJECT_DIR, args.project, create=True)
    try:
        for spec in specs:
            import_program(project, spec, program_image(spec, records, blobs))
            print(f"imported {spec.import_name} from {spec.record}")

        for spec in specs:
            with pyghidra.program_context(project, spec.project_path) as program:
                seed_count = setup_program(
                    program,
                    spec,
                    blobs,
                    seeds[spec.stage],
                    entries.get(spec.stage, ()),
                )
                elapsed = analyze_program(program, args.analysis_timeout)
                program.save(
                    f"Intellifex LTD {spec.stage} analysis", pyghidra.task_monitor()
                )
                report(program, spec, seed_count, elapsed)
                export_program(program, spec, ANALYSIS_DIR)
                print(f"  exported to {ANALYSIS_DIR}")
    finally:
        project.close()
        # Ghidra's create path does not release the project lock on close, which makes
        # the GUI ask to break a lock nobody holds. The files are advisory only.
        for leftover in (gpr.with_suffix(".lock"), gpr.with_suffix(".lock~")):
            leftover.unlink(missing_ok=True)

    print(f"project written to {PROJECT_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
