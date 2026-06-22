#include "power.h"
#include "../../include/io.h"
#include "../../include/kernel.h"
#include "../../include/multiboot2.h"
#include "../../arch/x86_64/boot/pvh.h"
#include "../../lib/kprintf.h"
#include "../../lib/string.h"

#define ACPI_PM1_CNT_SCI_EN  0x0001
#define ACPI_PM1_CNT_SLP_EN  0x2000
#define ACPI_PM1_CNT_SLP_SHIFT 10

#define MB2_TAG_ACPI_OLD 14
#define MB2_TAG_ACPI_NEW 15

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} acpi_rsdp_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

typedef struct __attribute__((packed)) {
    acpi_sdt_header_t h;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
} acpi_fadt_t;

typedef struct {
    bool available;
    uint16_t pm1a_cnt;
    uint16_t pm1b_cnt;
    uint8_t slp_typ_a;
    uint8_t slp_typ_b;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
} power_state_t;

static power_state_t power_state;

static bool sig_eq(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static bool checksum_ok(const void *data, uint32_t length) {
    const uint8_t *p = (const uint8_t *)data;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++)
        sum = (uint8_t)(sum + p[i]);
    return sum == 0;
}

static uint32_t aml_pkg_len_size(uint8_t first) {
    return ((first >> 6) & 0x03) + 1;
}

static uint32_t aml_read_pkg_len(const uint8_t *p) {
    uint8_t first = p[0];
    uint32_t bytes = aml_pkg_len_size(first);
    uint32_t value = first & 0x3F;

    if (bytes == 1)
        return value;

    value = first & 0x0F;
    for (uint32_t i = 1; i < bytes; i++)
        value |= (uint32_t)p[i] << (4 + ((i - 1) * 8));
    return value;
}

static bool aml_read_int(const uint8_t **p, const uint8_t *end, uint8_t *out) {
    if (*p >= end) return false;

    uint8_t op = *(*p)++;
    if (op == 0x00) { *out = 0; return true; }
    if (op == 0x01) { *out = 1; return true; }
    if (op == 0x0A) {
        if (*p >= end) return false;
        *out = *(*p)++;
        return true;
    }
    if (op == 0x0B) {
        if (*p + 2 > end) return false;
        *out = (*p)[0];
        *p += 2;
        return true;
    }

    *out = op;
    return true;
}

bool acpi_parse_s5_package(const uint8_t *aml, uint32_t length,
                           uint8_t *slp_typ_a, uint8_t *slp_typ_b) {
    if (!aml || !slp_typ_a || !slp_typ_b || length < 8)
        return false;

    for (uint32_t i = 0; i + 8 < length; i++) {
        if (aml[i] != '_' || aml[i + 1] != 'S' ||
            aml[i + 2] != '5' || aml[i + 3] != '_') {
            continue;
        }

        const uint8_t *p = &aml[i + 4];
        const uint8_t *end = aml + length;
        if (p >= end || *p != 0x12)
            continue;
        p++;

        if (p >= end) continue;
        uint32_t pkg_bytes = aml_pkg_len_size(*p);
        uint32_t pkg_len = aml_read_pkg_len(p);
        p += pkg_bytes;

        if (p >= end) continue;
        uint8_t elements = *p++;
        (void)elements;

        const uint8_t *pkg_end = p + pkg_len;
        if (pkg_end > end)
            pkg_end = end;

        uint8_t a = 0;
        uint8_t b = 0;
        if (!aml_read_int(&p, pkg_end, &a))
            continue;
        if (!aml_read_int(&p, pkg_end, &b))
            b = a;

        *slp_typ_a = a;
        *slp_typ_b = b;
        return true;
    }
    return false;
}

static const acpi_rsdp_t *find_rsdp_mb2(uint32_t info_phys) {
    const mb2_info_t *info = (const mb2_info_t *)(uintptr_t)info_phys;
    const uint8_t *p = (const uint8_t *)(info + 1);
    const uint8_t *end = (const uint8_t *)info + info->total_size;

    while (p < end) {
        const mb2_tag_t *tag = (const mb2_tag_t *)p;
        if (tag->type == MB2_TAG_END) break;
        if (tag->type == MB2_TAG_ACPI_OLD || tag->type == MB2_TAG_ACPI_NEW)
            return (const acpi_rsdp_t *)(tag + 1);
        p += ALIGN_UP(tag->size, 8);
    }
    return NULL;
}

static const acpi_rsdp_t *find_rsdp_pvh(uint32_t info_phys) {
    const hvm_start_info_t *si =
        (const hvm_start_info_t *)(uintptr_t)info_phys;

    if (info_phys != 0 && si->magic == HVM_START_MAGIC && si->rsdp_paddr)
        return (const acpi_rsdp_t *)(uintptr_t)si->rsdp_paddr;
    return NULL;
}

static const acpi_sdt_header_t *find_table_rsdt(const acpi_sdt_header_t *rsdt,
                                                const char *signature) {
    if (!rsdt || !sig_eq(rsdt->signature, "RSDT", 4))
        return NULL;

    uint32_t count = (rsdt->length - sizeof(*rsdt)) / sizeof(uint32_t);
    const uint32_t *entries = (const uint32_t *)(rsdt + 1);
    for (uint32_t i = 0; i < count; i++) {
        const acpi_sdt_header_t *h =
            (const acpi_sdt_header_t *)(uintptr_t)entries[i];
        if (h && sig_eq(h->signature, signature, 4))
            return h;
    }
    return NULL;
}

static const acpi_sdt_header_t *find_table_xsdt(const acpi_sdt_header_t *xsdt,
                                                const char *signature) {
    if (!xsdt || !sig_eq(xsdt->signature, "XSDT", 4))
        return NULL;

    uint32_t count = (xsdt->length - sizeof(*xsdt)) / sizeof(uint64_t);
    const uint64_t *entries = (const uint64_t *)(xsdt + 1);
    for (uint32_t i = 0; i < count; i++) {
        const acpi_sdt_header_t *h =
            (const acpi_sdt_header_t *)(uintptr_t)entries[i];
        if (h && sig_eq(h->signature, signature, 4))
            return h;
    }
    return NULL;
}

void power_init(uint32_t boot_magic, uint32_t boot_info_phys) {
    memset(&power_state, 0, sizeof(power_state));

    const acpi_rsdp_t *rsdp = NULL;
    if (boot_magic == MB2_BOOTLOADER_MAGIC)
        rsdp = find_rsdp_mb2(boot_info_phys);
    else
        rsdp = find_rsdp_pvh(boot_info_phys);

    if (!rsdp || !sig_eq(rsdp->signature, "RSD PTR ", 8) ||
        !checksum_ok(rsdp, 20)) {
        kprintf("[boot] Power: ACPI RSDP not found\n");
        return;
    }

    const acpi_sdt_header_t *fadt_h = NULL;
    if (rsdp->revision >= 2 && rsdp->xsdt_addr) {
        const acpi_sdt_header_t *xsdt =
            (const acpi_sdt_header_t *)(uintptr_t)rsdp->xsdt_addr;
        fadt_h = find_table_xsdt(xsdt, "FACP");
    }
    if (!fadt_h && rsdp->rsdt_addr) {
        const acpi_sdt_header_t *rsdt =
            (const acpi_sdt_header_t *)(uintptr_t)rsdp->rsdt_addr;
        fadt_h = find_table_rsdt(rsdt, "FACP");
    }
    if (!fadt_h || !checksum_ok(fadt_h, fadt_h->length)) {
        kprintf("[boot] Power: ACPI FADT not found\n");
        return;
    }

    const acpi_fadt_t *fadt = (const acpi_fadt_t *)fadt_h;
    const acpi_sdt_header_t *dsdt =
        (const acpi_sdt_header_t *)(uintptr_t)fadt->dsdt;
    if (!dsdt || !sig_eq(dsdt->signature, "DSDT", 4)) {
        kprintf("[boot] Power: ACPI DSDT not found\n");
        return;
    }

    const uint8_t *aml = (const uint8_t *)(dsdt + 1);
    uint32_t aml_len = dsdt->length - sizeof(*dsdt);
    uint8_t slp_typ_a = 0;
    uint8_t slp_typ_b = 0;
    if (!acpi_parse_s5_package(aml, aml_len, &slp_typ_a, &slp_typ_b)) {
        kprintf("[boot] Power: ACPI _S5_ package not found\n");
        return;
    }

    power_state.available = fadt->pm1a_cnt_blk != 0;
    power_state.pm1a_cnt = (uint16_t)fadt->pm1a_cnt_blk;
    power_state.pm1b_cnt = (uint16_t)fadt->pm1b_cnt_blk;
    power_state.slp_typ_a = slp_typ_a;
    power_state.slp_typ_b = slp_typ_b;
    power_state.smi_cmd = fadt->smi_cmd;
    power_state.acpi_enable = fadt->acpi_enable;

    if (power_state.available) {
        kprintf("[boot] Power: ACPI S5 ready (PM1a=0x%x type=%u)\n",
                power_state.pm1a_cnt, power_state.slp_typ_a);
    } else {
        kprintf("[boot] Power: ACPI S5 unavailable\n");
    }
}

bool power_has_acpi_shutdown(void) {
    return power_state.available;
}

void power_shutdown(void) {
    kprintf("Shutting down...\n");

    if (power_state.available) {
        if (power_state.smi_cmd && power_state.acpi_enable &&
            (inw(power_state.pm1a_cnt) & ACPI_PM1_CNT_SCI_EN) == 0) {
            outb((uint16_t)power_state.smi_cmd, power_state.acpi_enable);
            for (uint32_t i = 0; i < 100000; i++) io_wait();
        }

        uint16_t value_a =
            (uint16_t)((power_state.slp_typ_a << ACPI_PM1_CNT_SLP_SHIFT) |
                       ACPI_PM1_CNT_SLP_EN);
        outw(power_state.pm1a_cnt, value_a);

        if (power_state.pm1b_cnt) {
            uint16_t value_b =
                (uint16_t)((power_state.slp_typ_b << ACPI_PM1_CNT_SLP_SHIFT) |
                           ACPI_PM1_CNT_SLP_EN);
            outw(power_state.pm1b_cnt, value_b);
        }
    }

    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    kprintf("Power-off request failed; halting CPU.\n");
    for (;;) __asm__ volatile ("hlt");
}
