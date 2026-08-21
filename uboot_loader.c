/*
 * uboot_loader.c
 *
 * Freestanding AArch64 RAM loader for U-Boot "go" command.
 *
 * Receives a binary stream over serial in CHDR format, writes
 * it to a fixed destination address, validates CRC32, and
 * returns cleanly to U-Boot.
 *
 * Build-time configuration (edit before compiling):
 *   LOADER_LOAD_ADDR  - Address where THIS binary is loaded (for linker script)
 *   DEST_ADDR         - Address where received payload is written
 *   MAX_SIZE          - Maximum accepted payload size in bytes
 */

/* ---------------------------------------------------------------
 * Build-time configuration
 * --------------------------------------------------------------- */
#define DEST_ADDR   0x01080000UL  /* RAM destination for payload  */
#define MAX_SIZE    0x01000000UL   /* 16 MiB hard cap              */

/* ---------------------------------------------------------------
 * U-Boot runtime function pointers (already-relocated addresses)
 * --------------------------------------------------------------- */
typedef int  (*getc_fn)(void);
typedef int  (*tstc_fn)(void);
typedef void (*putc_fn)(char);
typedef void (*puts_fn)(const char *);

static const getc_fn uboot_getc = (getc_fn)0x1ff25c80;
static const tstc_fn uboot_tstc = (tstc_fn)0x1ff25c98;
static const putc_fn uboot_putc = (putc_fn)0x1ff25cb0;
static const puts_fn uboot_puts = (puts_fn)0x1ff25cd8;

/* ---------------------------------------------------------------
 * Suppress unused-variable warning for tstc (reserved for future
 * use per spec; referenced here so the compiler keeps the symbol)
 * --------------------------------------------------------------- */
static void use_tstc_ref(void) { (void)uboot_tstc; }

/* ---------------------------------------------------------------
 * Output helpers
 * --------------------------------------------------------------- */

/* Print a single uppercase hex nibble */
static void print_nibble(unsigned int n)
{
    n &= 0xf;
    if (n < 10)
        uboot_putc((char)('0' + n));
    else
        uboot_putc((char)('a' + n - 10));
}

/* Print a 32-bit value as "0x????????\n" */
static void print_hex32(unsigned long val)
{
    uboot_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        print_nibble((unsigned int)((val >> shift) & 0xf));
    uboot_putc('\n');
}

/* Print a 64-bit address as "0x????????????????\n" */
static void print_hex64(unsigned long long val)
{
    uboot_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        print_nibble((unsigned int)((val >> shift) & 0xf));
    uboot_putc('\n');
}

/* ---------------------------------------------------------------
 * Serial I/O helpers
 * --------------------------------------------------------------- */

/* Blocking read of exactly one byte */
static unsigned int read_byte(void)
{
    return (unsigned int)(uboot_getc() & 0xff);
}

/* Read a 4-byte big-endian unsigned integer from serial */
static unsigned long read_be32(void)
{
    unsigned long b0 = read_byte(); /* MSB */
    unsigned long b1 = read_byte();
    unsigned long b2 = read_byte();
    unsigned long b3 = read_byte(); /* LSB */
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

/* ---------------------------------------------------------------
 * CRC32
 *
 * Reflected polynomial: 0xEDB88320
 * Initial value: 0xFFFFFFFF
 * Final XOR:     0xFFFFFFFF
 * --------------------------------------------------------------- */
static unsigned long crc32_byte(unsigned long crc, unsigned int byte)
{
    crc ^= byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 1UL)
            crc = (crc >> 1) ^ 0xEDB88320UL;
        else
            crc >>= 1;
    }
    return crc;
}

static unsigned long crc32_compute(const unsigned char *buf, unsigned long len)
{
    unsigned long crc = 0xFFFFFFFFUL;
    for (unsigned long i = 0; i < len; i++)
        crc = crc32_byte(crc, buf[i]);
    return crc ^ 0xFFFFFFFFUL;
}

/* ---------------------------------------------------------------
 * CHDR header magic bytes
 * --------------------------------------------------------------- */
#define MAGIC_C 0x43u
#define MAGIC_H 0x48u
#define MAGIC_D 0x44u
#define MAGIC_R 0x52u

/* ---------------------------------------------------------------
 * Entry point
 *
 * U-Boot's "go" command branches here directly (AArch64 AAPCS64).
 * --------------------------------------------------------------- */
__attribute__((section(".text._start"))) int _start(void)
{
    /* Silence unused reference to tstc */
    use_tstc_ref();

    uboot_puts("\n[loader] Waiting for CHDR stream...\n");

    /* ----------------------------------------------------------
     * Step 1: Read and validate the 16-byte CHDR header
     * ---------------------------------------------------------- */

    /* Magic: "CHDR" (4 bytes, big-endian ASCII) */
    unsigned int m0 = read_byte();
    unsigned int m1 = read_byte();
    unsigned int m2 = read_byte();
    unsigned int m3 = read_byte();

    if (m0 != MAGIC_C || m1 != MAGIC_H || m2 != MAGIC_D || m3 != MAGIC_R) {
        uboot_puts("[loader] ERROR: bad magic\n");
        return 1;
    }

    /* Reserved field: must be 0x00000000 */
    unsigned long reserved = read_be32();
    if (reserved != 0x00000000UL) {
        uboot_puts("[loader] ERROR: reserved field non-zero\n");
        return 1;
    }

    /* CRC32 of payload (big-endian) */
    unsigned long expected_crc = read_be32();

    /* Payload size in bytes (big-endian) */
    unsigned long payload_size = read_be32();

    /* ----------------------------------------------------------
     * Step 2: Validate payload size
     * ---------------------------------------------------------- */
    if (payload_size == 0) {
        uboot_puts("[loader] ERROR: payload size is zero\n");
        return 1;
    }
    if (payload_size > MAX_SIZE) {
        uboot_puts("[loader] ERROR: payload size exceeds MAX_SIZE\n");
        return 1;
    }

    /* ----------------------------------------------------------
     * Step 3: Receive payload and write directly to DEST_ADDR
     * ---------------------------------------------------------- */
    volatile unsigned char * const dest =
        (volatile unsigned char *)DEST_ADDR;

    uboot_puts("[loader] Receiving payload...\n");

    for (unsigned long i = 0; i < payload_size; i++)
        dest[i] = (unsigned char)read_byte();

    uboot_puts("[loader] Receive complete.\n");

    /* ----------------------------------------------------------
     * Step 4: CRC32 validation
     * ---------------------------------------------------------- */
    unsigned long calc_crc =
        crc32_compute((const unsigned char *)DEST_ADDR, payload_size);

    /* ----------------------------------------------------------
     * Step 5: Report result
     * ---------------------------------------------------------- */
    uboot_puts("[loader] dest addr : ");
    print_hex64((unsigned long long)DEST_ADDR);

    uboot_puts("[loader] payload sz: ");
    print_hex32(payload_size);

    uboot_puts("[loader] expect CRC: ");
    print_hex32(expected_crc);

    uboot_puts("[loader] calc   CRC: ");
    print_hex32(calc_crc);

    if (calc_crc == expected_crc) {
        uboot_puts("[loader] SUCCESS: CRC32 match. Payload is valid.\n");
        return 0;
    } else {
        uboot_puts("[loader] ERROR: CRC32 mismatch! Payload corrupted.\n");
        return 1;
    }
}
