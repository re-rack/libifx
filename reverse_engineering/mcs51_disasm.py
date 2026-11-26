"""MCS-51 (8051) decoder, recursive-descent walker and listing writer.

Operands follow Intel/MASM style: named SFRs and bits in upper case, everything
else as hex with an `h` suffix. Absolute addresses are always four digits so that
`0Fh` never needs a leading zero for the assembler.
"""

from typing import NamedTuple

SFR_NAMES = {
    0x80: "P0",
    0x81: "SP",
    0x82: "DPL",
    0x83: "DPH",
    0x87: "PCON",
    0x88: "TCON",
    0x89: "TMOD",
    0x8A: "TL0",
    0x8B: "TL1",
    0x8C: "TH0",
    0x8D: "TH1",
    0x90: "P1",
    0x98: "SCON",
    0x99: "SBUF",
    0xA0: "P2",
    0xA8: "IE",
    0xB0: "P3",
    0xB8: "IP",
    0xD0: "PSW",
    0xE0: "ACC",
    0xF0: "B",
}

BIT_NAMES = {
    0x88: "IT0",
    0x89: "IE0",
    0x8A: "IT1",
    0x8B: "IE1",
    0x8C: "TR0",
    0x8D: "TF0",
    0x8E: "TR1",
    0x8F: "TF1",
    0x98: "RI",
    0x99: "TI",
    0x9A: "RB8",
    0x9B: "TB8",
    0x9C: "REN",
    0x9D: "SM2",
    0x9E: "SM1",
    0x9F: "SM0",
    0xA8: "EX0",
    0xA9: "ET0",
    0xAA: "EX1",
    0xAB: "ET1",
    0xAC: "ES",
    0xAF: "EA",
    0xB8: "PX0",
    0xB9: "PT0",
    0xBA: "PX1",
    0xBB: "PT1",
    0xBC: "PS",
    0xD2: "OV",
    0xD3: "RS0",
    0xD4: "RS1",
    0xD5: "F0",
    0xD6: "AC",
    0xD7: "CY",
}

# mnemonic, operand templates. a16/a11/rel take a byte or two, direct/bit/#imm
# one, Rn/@Ri none.
TABLE = (
    ("nop", ()),
    ("ajmp", ("a11",)),
    ("ljmp", ("a16",)),
    ("rr", ("A",)),
    ("inc", ("A",)),
    ("inc", ("direct",)),
    ("inc", ("@Ri",)),
    ("inc", ("@Ri",)),
    ("inc", ("Rn",)),
    ("inc", ("Rn",)),
    ("inc", ("Rn",)),
    ("inc", ("Rn",)),
    ("inc", ("Rn",)),
    ("inc", ("Rn",)),
    ("inc", ("Rn",)),
    ("inc", ("Rn",)),
    ("jbc", ("bit", "rel")),
    ("acall", ("a11",)),
    ("lcall", ("a16",)),
    ("rrc", ("A",)),
    ("dec", ("A",)),
    ("dec", ("direct",)),
    ("dec", ("@Ri",)),
    ("dec", ("@Ri",)),
    ("dec", ("Rn",)),
    ("dec", ("Rn",)),
    ("dec", ("Rn",)),
    ("dec", ("Rn",)),
    ("dec", ("Rn",)),
    ("dec", ("Rn",)),
    ("dec", ("Rn",)),
    ("dec", ("Rn",)),
    ("jb", ("bit", "rel")),
    ("ajmp", ("a11",)),
    ("ret", ()),
    ("rl", ("A",)),
    ("add", ("A", "#imm")),
    ("add", ("A", "direct")),
    ("add", ("A", "@Ri")),
    ("add", ("A", "@Ri")),
    ("add", ("A", "Rn")),
    ("add", ("A", "Rn")),
    ("add", ("A", "Rn")),
    ("add", ("A", "Rn")),
    ("add", ("A", "Rn")),
    ("add", ("A", "Rn")),
    ("add", ("A", "Rn")),
    ("add", ("A", "Rn")),
    ("jnb", ("bit", "rel")),
    ("acall", ("a11",)),
    ("reti", ()),
    ("rlc", ("A",)),
    ("addc", ("A", "#imm")),
    ("addc", ("A", "direct")),
    ("addc", ("A", "@Ri")),
    ("addc", ("A", "@Ri")),
    ("addc", ("A", "Rn")),
    ("addc", ("A", "Rn")),
    ("addc", ("A", "Rn")),
    ("addc", ("A", "Rn")),
    ("addc", ("A", "Rn")),
    ("addc", ("A", "Rn")),
    ("addc", ("A", "Rn")),
    ("addc", ("A", "Rn")),
    ("jc", ("rel",)),
    ("ajmp", ("a11",)),
    ("orl", ("direct", "A")),
    ("orl", ("direct", "#imm")),
    ("orl", ("A", "#imm")),
    ("orl", ("A", "direct")),
    ("orl", ("A", "@Ri")),
    ("orl", ("A", "@Ri")),
    ("orl", ("A", "Rn")),
    ("orl", ("A", "Rn")),
    ("orl", ("A", "Rn")),
    ("orl", ("A", "Rn")),
    ("orl", ("A", "Rn")),
    ("orl", ("A", "Rn")),
    ("orl", ("A", "Rn")),
    ("orl", ("A", "Rn")),
    ("jnc", ("rel",)),
    ("acall", ("a11",)),
    ("anl", ("direct", "A")),
    ("anl", ("direct", "#imm")),
    ("anl", ("A", "#imm")),
    ("anl", ("A", "direct")),
    ("anl", ("A", "@Ri")),
    ("anl", ("A", "@Ri")),
    ("anl", ("A", "Rn")),
    ("anl", ("A", "Rn")),
    ("anl", ("A", "Rn")),
    ("anl", ("A", "Rn")),
    ("anl", ("A", "Rn")),
    ("anl", ("A", "Rn")),
    ("anl", ("A", "Rn")),
    ("anl", ("A", "Rn")),
    ("jz", ("rel",)),
    ("ajmp", ("a11",)),
    ("xrl", ("direct", "A")),
    ("xrl", ("direct", "#imm")),
    ("xrl", ("A", "#imm")),
    ("xrl", ("A", "direct")),
    ("xrl", ("A", "@Ri")),
    ("xrl", ("A", "@Ri")),
    ("xrl", ("A", "Rn")),
    ("xrl", ("A", "Rn")),
    ("xrl", ("A", "Rn")),
    ("xrl", ("A", "Rn")),
    ("xrl", ("A", "Rn")),
    ("xrl", ("A", "Rn")),
    ("xrl", ("A", "Rn")),
    ("xrl", ("A", "Rn")),
    ("jnz", ("rel",)),
    ("acall", ("a11",)),
    ("orl", ("C", "bit")),
    ("jmp", ("@A+DPTR",)),
    ("mov", ("A", "#imm")),
    ("mov", ("direct", "#imm")),
    ("mov", ("@Ri", "#imm")),
    ("mov", ("@Ri", "#imm")),
    ("mov", ("Rn", "#imm")),
    ("mov", ("Rn", "#imm")),
    ("mov", ("Rn", "#imm")),
    ("mov", ("Rn", "#imm")),
    ("mov", ("Rn", "#imm")),
    ("mov", ("Rn", "#imm")),
    ("mov", ("Rn", "#imm")),
    ("mov", ("Rn", "#imm")),
    ("sjmp", ("rel",)),
    ("ajmp", ("a11",)),
    ("anl", ("C", "bit")),
    ("movc", ("A", "@A+PC")),
    ("div", ("AB",)),
    ("mov", ("direct", "direct")),
    ("mov", ("@Ri", "direct")),
    ("mov", ("@Ri", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("DPTR", "#imm16")),
    ("acall", ("a11",)),
    ("mov", ("bit", "C")),
    ("movc", ("A", "@A+DPTR")),
    ("subb", ("A", "#imm")),
    ("subb", ("A", "direct")),
    ("subb", ("A", "@Ri")),
    ("subb", ("A", "@Ri")),
    ("subb", ("A", "Rn")),
    ("subb", ("A", "Rn")),
    ("subb", ("A", "Rn")),
    ("subb", ("A", "Rn")),
    ("subb", ("A", "Rn")),
    ("subb", ("A", "Rn")),
    ("subb", ("A", "Rn")),
    ("subb", ("A", "Rn")),
    ("orl", ("C", "/bit")),
    ("ajmp", ("a11",)),
    ("mov", ("C", "bit")),
    ("inc", ("DPTR",)),
    ("mul", ("AB",)),
    (None, ()),
    ("mov", ("@Ri", "direct")),
    ("mov", ("@Ri", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("mov", ("Rn", "direct")),
    ("anl", ("C", "/bit")),
    ("acall", ("a11",)),
    ("cpl", ("bit",)),
    ("cpl", ("C",)),
    ("cjne", ("A", "#imm", "rel")),
    ("cjne", ("A", "direct", "rel")),
    ("cjne", ("@Ri", "#imm", "rel")),
    ("cjne", ("@Ri", "#imm", "rel")),
    ("cjne", ("Rn", "#imm", "rel")),
    ("cjne", ("Rn", "#imm", "rel")),
    ("cjne", ("Rn", "#imm", "rel")),
    ("cjne", ("Rn", "#imm", "rel")),
    ("cjne", ("Rn", "#imm", "rel")),
    ("cjne", ("Rn", "#imm", "rel")),
    ("cjne", ("Rn", "#imm", "rel")),
    ("cjne", ("Rn", "#imm", "rel")),
    ("push", ("direct",)),
    ("ajmp", ("a11",)),
    ("clr", ("bit",)),
    ("clr", ("C",)),
    ("swap", ("A",)),
    ("xch", ("A", "direct")),
    ("xch", ("A", "@Ri")),
    ("xch", ("A", "@Ri")),
    ("xch", ("A", "Rn")),
    ("xch", ("A", "Rn")),
    ("xch", ("A", "Rn")),
    ("xch", ("A", "Rn")),
    ("xch", ("A", "Rn")),
    ("xch", ("A", "Rn")),
    ("xch", ("A", "Rn")),
    ("xch", ("A", "Rn")),
    ("pop", ("direct",)),
    ("acall", ("a11",)),
    ("setb", ("bit",)),
    ("setb", ("C",)),
    ("da", ("A",)),
    ("djnz", ("direct", "rel")),
    ("xchd", ("A", "@Ri")),
    ("xchd", ("A", "@Ri")),
    ("djnz", ("Rn", "rel")),
    ("djnz", ("Rn", "rel")),
    ("djnz", ("Rn", "rel")),
    ("djnz", ("Rn", "rel")),
    ("djnz", ("Rn", "rel")),
    ("djnz", ("Rn", "rel")),
    ("djnz", ("Rn", "rel")),
    ("djnz", ("Rn", "rel")),
    ("movx", ("A", "@DPTR")),
    ("ajmp", ("a11",)),
    ("movx", ("A", "@R0")),
    ("movx", ("A", "@R1")),
    ("clr", ("A",)),
    ("mov", ("A", "direct")),
    ("mov", ("A", "@Ri")),
    ("mov", ("A", "@Ri")),
    ("mov", ("A", "Rn")),
    ("mov", ("A", "Rn")),
    ("mov", ("A", "Rn")),
    ("mov", ("A", "Rn")),
    ("mov", ("A", "Rn")),
    ("mov", ("A", "Rn")),
    ("mov", ("A", "Rn")),
    ("mov", ("A", "Rn")),
    ("movx", ("@DPTR", "A")),
    ("acall", ("a11",)),
    ("movx", ("@R0", "A")),
    ("movx", ("@R1", "A")),
    ("cpl", ("A",)),
    ("mov", ("direct", "A")),
    ("mov", ("@Ri", "A")),
    ("mov", ("@Ri", "A")),
    ("mov", ("Rn", "A")),
    ("mov", ("Rn", "A")),
    ("mov", ("Rn", "A")),
    ("mov", ("Rn", "A")),
    ("mov", ("Rn", "A")),
    ("mov", ("Rn", "A")),
    ("mov", ("Rn", "A")),
    ("mov", ("Rn", "A")),
)

UNDEFINED = f"{0xA5:02X}h"

CALLS = frozenset({"lcall", "acall"})

BRANCHES = (
    frozenset({"ajmp", "ljmp", "sjmp", "jc", "jnc", "jz", "jnz", "jbc", "jb", "jnb"})
    | CALLS
)

# A path continues past a conditional jump but not past these.
TERMINATORS = frozenset({"ret", "reti", "ajmp", "ljmp", "sjmp", "jmp"})

OP_TEMPLATE_SIZE = {
    "a16": 2,
    "#imm16": 2,
    "a11": 1,
    "rel": 1,
    "direct": 1,
    "bit": 1,
    "/bit": 1,
    "#imm": 1,
}


class Instruction(NamedTuple):
    address: int
    length: int
    mnemonic: str
    operands: tuple[str, ...]
    target: int | None

    @property
    def text(self) -> str:
        return f"{self.mnemonic:<6}{','.join(self.operands)}"

    @property
    def is_call(self) -> bool:
        return self.mnemonic in CALLS

    @property
    def is_branch(self) -> bool:
        return self.mnemonic in BRANCHES

    @property
    def ends_path(self) -> bool:
        return self.mnemonic in TERMINATORS


class JumpTable(NamedTuple):
    site: int
    address: int
    entry_size: int
    entries: int
    targets: tuple[int, ...]


class Analysis(NamedTuple):
    instructions: dict[int, Instruction]
    tables: tuple[JumpTable, ...]
    unresolved: tuple[int, ...]


def direct(address: int) -> str:
    return SFR_NAMES.get(address, f"{address:02X}h")


def bit_name(address: int) -> str:
    if address in BIT_NAMES:
        return BIT_NAMES[address]
    if address < 0x80:
        return f"{0x20 + address // 8:02X}h.{address % 8}"
    return f"{direct(address & 0xF8)}.{address % 8}"


def decode(image: bytes, address: int, end: int) -> Instruction | None:
    """One instruction at `address`, or None when undefined or cut off by `end`."""
    opcode = image[address]
    mnemonic, templates = TABLE[opcode]
    if mnemonic is None:
        return None
    operands: list[str] = []
    target: int | None = None
    cursor = address + 1
    for template in templates:
        size = OP_TEMPLATE_SIZE.get(template, 0)
        if size and cursor + size - 1 > end:
            return None
        raw = image[cursor] if size == 1 else None
        if size == 2:
            raw = int.from_bytes(image[cursor : cursor + 2], "big")
        cursor += size
        if template == "Rn":
            operands.append(f"R{opcode & 7}")
        elif template == "@Ri":
            operands.append(f"@R{opcode & 1}")
        elif template == "#imm":
            operands.append(f"#{raw:02X}h")
        elif template == "#imm16":
            operands.append(f"#{raw:04X}h")
        elif template == "direct":
            operands.append(direct(raw))
        elif template == "bit":
            operands.append(bit_name(raw))
        elif template == "/bit":
            operands.append(f"/{bit_name(raw)}")
        elif template == "rel":
            target = cursor + raw - 0x100 if raw > 0x7F else cursor + raw
            operands.append(f"{target:04X}h")
        elif template == "a16":
            target = raw
            operands.append(f"{raw:04X}h")
        elif template == "a11":
            # addr11 keeps the current 2 KiB page of the address after the jump.
            target = (cursor & 0xF800) | ((opcode & 0xE0) << 3) | raw
            operands.append(f"{target:04X}h")
        else:
            operands.append(template)
    if opcode == 0x85:
        # 85h stores the source byte before the destination byte.
        operands.reverse()
    return Instruction(address, cursor - address, mnemonic, tuple(operands), target)


# shortcut: only the mov DPTR,#table / anl A,#mask / jmp @A+DPTR shape is resolved; any
# other table is counted in Analysis.unresolved, so the report shows it.
def _jump_table(
    image: bytes, site: int, dptr: int, mask: int | None, end: int
) -> JumpTable:
    """Entries at `dptr` while they stay AJMP (2 bytes) or LJMP (3 bytes)."""
    if image[dptr] & 0x1F == 0x01:
        size = 2
    elif image[dptr] == 0x02:
        size = 3
    else:
        raise ValueError(f"no AJMP or LJMP entry at {dptr:04X}h")
    count = mask + 1 if mask is not None else 0
    targets: list[int] = []
    while mask is None or len(targets) < count:
        entry = dptr + size * len(targets)
        if entry + size - 1 > end:
            break
        if size == 2:
            if image[entry] & 0x1F != 0x01:
                break
            target = (
                ((entry + 2) & 0xF800) | ((image[entry] & 0xE0) << 3) | image[entry + 1]
            )
        else:
            if image[entry] != 0x02:
                break
            target = int.from_bytes(image[entry + 1 : entry + 3], "big")
        targets.append(target)
    if mask is not None and len(targets) < count:
        raise ValueError(
            f"jump table at {dptr:04X}h holds {len(targets)} entries, "
            f"ANL A,#{mask:02X}h asks for {count}"
        )
    return JumpTable(site, dptr, size, len(targets), tuple(targets))


def descend(image: bytes, entries: tuple[int, ...], end: int) -> Analysis:
    """Walk every path reachable from `entries`, following branches and jump tables."""
    instructions: dict[int, Instruction] = {}
    tables: list[JumpTable] = []
    unresolved: list[int] = []
    todo = list(entries)
    while todo:
        address = todo.pop()
        dptr: int | None = None
        mask: int | None = None
        while 0 <= address <= end and address not in instructions:
            instruction = decode(image, address, end)
            if instruction is None:
                break
            instructions[address] = instruction
            if instruction.target is not None:
                todo.append(instruction.target)
            if instruction.mnemonic == "mov" and instruction.operands[0] == "DPTR":
                dptr = int(instruction.operands[1][1:-1], 16)
            elif (
                instruction.mnemonic == "anl"
                and instruction.operands[0] == "A"
                and instruction.operands[1].startswith("#")
            ):
                mask = int(instruction.operands[1][1:-1], 16)
            elif instruction.mnemonic == "jmp":
                try:
                    if dptr is None:
                        raise ValueError("no MOV DPTR in the block")
                    table = _jump_table(image, address, dptr, mask, end)
                except (ValueError, IndexError):
                    unresolved.append(address)
                else:
                    tables.append(table)
                    for index in range(table.entries):
                        entry = table.address + table.entry_size * index
                        decoded = decode(image, entry, end)
                        if decoded is not None:
                            instructions.setdefault(entry, decoded)
                    todo.extend(table.targets)
            if instruction.is_branch:
                dptr, mask = None, None
            if instruction.ends_path:
                break
            address += instruction.length
    return Analysis(instructions, tuple(tables), tuple(unresolved))


def _ascii(raw: bytes) -> str:
    return "".join(chr(b) if 0x20 <= b <= 0x7E else "." for b in raw)


def render(image: bytes, analysis: Analysis, start: int, end: int) -> str:
    """Listing of `start`..`end`: one line per instruction, `db` for the rest."""
    entries: dict[int, tuple[JumpTable, int]] = {}
    for table in analysis.tables:
        for index in range(table.entries):
            entries[table.address + table.entry_size * index] = (table, index)
    lines: list[str] = []
    address = start
    while address <= end:
        instruction = analysis.instructions.get(address)
        if instruction is not None:
            raw = image[address : address + instruction.length]
            hexed = " ".join(f"{b:02x}" for b in raw)
            line = f"C:{address:04X}  {hexed:<11}{instruction.text}".rstrip()
            entry = entries.get(address)
            if entry is not None:
                line += f"   ; jump table C:{entry[0].address:04X} entry {entry[1]}"
            lines.append(line)
            address += instruction.length
            continue
        stop = address
        while stop <= end and stop not in analysis.instructions:
            stop += 1
            if stop - address == 16:
                break
        raw = image[address:stop]
        hexed = " ".join(f"{b:02x}" for b in raw)
        lines.append(f"C:{address:04X}  {hexed:<47}; {_ascii(raw)}")
        address = stop
    return "\n".join(lines) + "\n"
