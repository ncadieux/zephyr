/*
 * Copyright (c) 2023 North River Systems Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Emulator for the TI INA237 I2C power monitor
 */
#ifndef INA237_EMUL_H_
#define INA237_EMUL_H_

#define INA237_REGISTER_COUNT 16

int ina237_mock_set_register(void *data_ptr, int reg, uint32_t value);
int ina237_mock_get_register(void *data_ptr, int reg, uint32_t *value_ptr);

void ina237_mock_reset_reads(void *data_ptr);
int ina237_mock_read_count(void *data_ptr, int reg);
void ina237_mock_fail_read(void *data_ptr, int reg);

#endif /* INA237_EMUL_H_ */
