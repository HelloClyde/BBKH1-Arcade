"""Audit assembled fixed-load coordinates; compare generated RGB32 images.

This is a host-side static instruction/coordinate test, not a guest CPU or
hardware emulator. Actual native execution is checked separately on H1.
"""
import argparse
from pathlib import Path
import os
import random
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def assembled_map(disassembly, name, repeat, source_step, output_step):
    body = disassembly.split(f"<{name}>:", 1)[1].split("Disassembly of section", 1)[0]
    instructions = re.findall(r"^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\w+)\s*([^\n]*)", body, re.M)
    assert instructions[0] == ("li", f"v0,{repeat}"), instructions[0]
    registers, coordinates, increments = {}, {}, []
    for operation, operands in instructions:
        assert operation in {"li", "lw", "sw", "addiu", "bnez", "jr", "nop"}, (operation, operands)
        if operation in {"lw", "sw"}:
            match = re.fullmatch(r"(t[0-7]),(\d+)\((a[01])\)", operands)
            assert match, operands
            register, offset, base = match.groups()
            offset = int(offset)
            assert offset % 4 == 0
            if operation == "lw":
                assert base == "a1" and offset // 4 < source_step
                registers[register] = offset // 4
            else:
                assert base == "a0" and offset // 4 < output_step
                assert offset // 4 not in coordinates
                coordinates[offset // 4] = registers[register]
        elif operation == "addiu":
            increments.append(operands)
        elif operation == "bnez":
            assert operands.startswith(f"v0,4 <{name}+0x4>")
        elif operation == "jr":
            assert operands == "ra"
    assert increments == [f"a1,a1,{source_step * 4}", "v0,v0,-1", f"a0,a0,{output_step * 4}"]
    assert set(coordinates) == set(range(output_step))
    result = [block * source_step + coordinates[x] for block in range(repeat) for x in range(output_step)]
    width = repeat * output_step
    assert result == [x * 448 // width for x in range(width)]
    return result, len(instructions) * 4


def check_images(columns, width):
    rng = random.Random(4740 + width)
    for frame in range(8):
        source = [rng.getrandbits(32) for _ in range(448 * 224)]
        # Black side margins are part of the presenter contract.
        actual = [0xA5A5A5A5] * (480 * 272)
        reference = [0] * (480 * 272)
        left = (480 - width) // 2
        for y in range(272):
            sy = y * 224 // 272
            base = sy * 448
            offset = y * 480
            actual[offset:offset + left] = [0] * left
            actual[offset + left:offset + left + width] = [source[base + sx] for sx in columns]
            actual[offset + left + width:offset + 480] = [0] * (480 - left - width)
            reference[offset + left:offset + left + width] = [source[base + x * 448 // width] for x in range(width)]
        assert actual == reference, (width, frame)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--toolchain", type=Path, default=ROOT.parent.parent / ".tools/toolchain/bin")
    args = parser.parse_args()
    suffix = ".exe" if os.name == "nt" else ""
    cc = args.toolchain / f"mipsel-none-elf-gcc{suffix}"
    objdump = args.toolchain / f"mipsel-none-elf-objdump{suffix}"
    with tempfile.TemporaryDirectory(prefix="pgm-scale-") as temporary:
        obj = Path(temporary) / "pgm_scale.o"
        subprocess.run([str(cc), "-c", "-EL", "-march=mips32", "-mabi=32", "-msoft-float", "-mno-abicalls", "-G0", "-fno-pic", str(ROOT / "src/pgm_scale.S"), "-o", str(obj)], check=True)
        assembly = subprocess.check_output([str(objdump), "-d", str(obj)], text=True)
    for width, repeat, source_step, output_step in [(362, 2, 224, 181), (480, 32, 14, 15)]:
        coordinates, size = assembled_map(assembly, f"h1_pgm_scale{width}32", repeat, source_step, output_step)
        check_images(coordinates, width)
        print(f"448->{width}: all assembled coordinates exact, {size} bytes, 8 generated full RGB32 images identical")
    print("Only caller-saved GPR loads/stores/arithmetic/control; no MXU, CP0 or interrupt changes.")


if __name__ == "__main__":
    main()
