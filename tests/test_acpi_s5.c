#include <assert.h>
#include <stdint.h>

#include "../kernel/power/power.h"

int main(void) {
    const uint8_t aml[] = {
        'x', 'x', '_', 'S', '5', '_',
        0x12, 0x0A, 0x04, 0x0A, 0x05, 0x0A, 0x05, 0x00, 0x00
    };

    uint8_t slp_typ_a = 0;
    uint8_t slp_typ_b = 0;
    assert(acpi_parse_s5_package(aml, sizeof(aml), &slp_typ_a, &slp_typ_b));
    assert(slp_typ_a == 5);
    assert(slp_typ_b == 5);

    const uint8_t compact_aml[] = {
        '_', 'S', '5', '_', 0x12, 0x06, 0x02, 0x01, 0x00
    };
    assert(acpi_parse_s5_package(compact_aml, sizeof(compact_aml),
                                 &slp_typ_a, &slp_typ_b));
    assert(slp_typ_a == 1);
    assert(slp_typ_b == 0);

    return 0;
}
