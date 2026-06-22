#pragma once

#include "../../include/types.h"

void power_init(uint32_t boot_magic, uint32_t boot_info_phys);
void power_shutdown(void);
bool power_has_acpi_shutdown(void);

bool acpi_parse_s5_package(const uint8_t *aml, uint32_t length,
                           uint8_t *slp_typ_a, uint8_t *slp_typ_b);
