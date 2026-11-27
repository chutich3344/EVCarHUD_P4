/* Module dong ho DS3231 (vd ZS-042, co pin CR2032) tren bus I2C chung (SDA 7, SCL 8), dia chi 0x68. Luu gio UTC. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2c_master.h"

bool ds3231_init(i2c_master_bus_handle_t bus);   /* false: khong gan module */
bool ds3231_read(int64_t *unix_utc);              /* false: chua co module / pin het (co OSF) / gio hong */
bool ds3231_write(int64_t unix_utc);
