# uboot_loader - AArch64 CHDR Serial Loader for U-Boot `go`

A minimal, freestanding AArch64 helper binary meant to be loaded into RAM and
executed via U-Boot's `go` command. Once running, it blocks on the serial
console waiting for a payload framed in a small custom protocol ("CHDR"),
writes that payload verbatim to a fixed destination address, verifies it with
CRC32, and returns cleanly to U-Boot.

This repo also contains:
* `loader.ld`: Linker script controlling load address and section layout.
* `chdr-sender.py`: Tiny python tool to send an image over the serial connection to the loader.
* `custom-repacker.py`: A script to pack an image with the custom CHDR header. 

## How it works

### Memory layout

| Symbol           | Value        | Meaning                                                   |
|-------------------|-------------|------------------------------------------------------------|
| Loader load addr  | `0x04000000` | Where `uboot_loader.bin` itself must be placed in RAM (set by `loader.ld`, `. = 0x04000000`) |
| `DEST_ADDR`       | `0x01080000` | Where the received *payload* is written                    |
| `MAX_SIZE`        | `0x01000000` (16 MiB) | Hard cap on accepted payload size                 |

### U-Boot entry points

The loader calls directly into already-relocated U-Boot runtime functions at
fixed absolute addresses, rather than linking against U-Boot's console code:

```c
uboot_getc = 0x1ff25c80   // int  getc(void)
uboot_tstc = 0x1ff25c98   // int  tstc(void)   (unused, kept referenced only)
uboot_putc = 0x1ff25cb0   // void putc(char)
uboot_puts = 0x1ff25cd8   // void puts(const char *)
```

**These addresses are board/U-Boot-build-specific.** They were resolved for
one particular U-Boot image and will need to be re-derived (from a symbol map
or disassembly of that U-Boot binary) for any other target/build. If you
retarget this loader, update these addresses first. Everything else in
the file is generic AArch64/freestanding code.

### Execution flow

1. Print a banner and block on `getc()` for the 4-byte magic.
2. Validate magic, reserved field, and payload size (reject `0` and
   anything `> MAX_SIZE`).
3. Read `payload_size` bytes one at a time from the serial console straight
   into `DEST_ADDR`.
4. Compute CRC32 over the bytes now sitting at `DEST_ADDR` and compare
   against the header's expected CRC.
5. Print destination address, payload size, expected CRC, and computed CRC.
6. Return `0` (success: CRC matched) or `1` (failure: bad magic, bad
   reserved field, bad size, or CRC mismatch) back to U-Boot's `go` caller.

## Building

Requires an `aarch64-linux-gnu` cross toolchain (`gcc`, `objcopy`).

```bash
# Set variables first
CROSS=aarch64-linux-gnu
SRC=uboot_loader.c
ELF=uboot_loader.elf
BIN=uboot_loader.bin
LD_SCRIPT=loader.ld

# Step 1: Compile to object
${CROSS}-gcc \
    -std=c11 \
    -ffreestanding \
    -nostdlib \
    -nostartfiles \
    -nodefaultlibs \
    -march=armv8-a \
    -mabi=lp64 \
    -O2 \
    -Wall -Wextra \
    -fno-stack-protector \
    -fno-asynchronous-unwind-tables \
    -fno-unwind-tables \
    -c ${SRC} -o uboot_loader.o

# Step 2: Link
${CROSS}-gcc \
    -nostdlib \
    -nostartfiles \
    -nodefaultlibs \
    -T ${LD_SCRIPT} \
    uboot_loader.o \
    -o ${ELF}

# Step 3: Strip to raw binary
${CROSS}-objcopy \
    -O binary \
    --strip-all \
    ${ELF} ${BIN}
```

This produces:

* `uboot_loader.o`: compiled object (intermediate)
* `uboot_loader.elf`: linked ELF with debug info/headers (intermediate;
  useful for disassembly/symbol inspection, not for loading)
* `uboot_loader.bin`: raw flat binary, stripped of all ELF overhead. This
  is the artifact you actually transfer to the board

### Sanity-checking the build

```bash
# Confirm _start is where loader.ld expects it (0x04000000) and
# that there's no unexpected data/bss bleeding past your RAM budget
${CROSS}-objdump -h uboot_loader.elf
${CROSS}-nm uboot_loader.elf | grep _start
```

## Loading and running on the target

Typical U-Boot session (adjust for your board's memory map / console):

```
=> go 0x04000000 (or whatever address you picked)
```

Once you see `[loader] Waiting for CHDR stream...`, send the 16-byte CHDR
header followed by the raw payload bytes over the same serial link (a small
Python script using `pyserial` is the easiest way to frame this).

On success:

```
[loader] Receiving payload...
[loader] Receive complete.
[loader] dest addr : 0x0000000001080000
[loader] payload sz: 0x00xxxxxx
[loader] expect CRC: 0x????????
[loader] calc   CRC: 0x????????
[loader] SUCCESS: CRC32 match. Payload is valid.
```
