#include "rtc_ds3231.h"
#include "esp_log.h"

static const char *TAG = "ds3231";
static i2c_master_dev_handle_t dev;

static int bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t tobcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

bool ds3231_init(i2c_master_bus_handle_t bus)
{
    if (!bus || i2c_master_probe(bus, 0x68, 50) != ESP_OK) {
        ESP_LOGI(TAG, "khong thay module RTC DS3231 (0x68)");
        return false;
    }
    i2c_device_config_t c = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x68, .scl_speed_hz = 100000};
    if (i2c_master_bus_add_device(bus, &c, &dev) != ESP_OK) { dev = NULL; return false; }
    ESP_LOGI(TAG, "co module RTC DS3231");
    return true;
}

bool ds3231_read(int64_t *unix_utc)
{
    if (!dev) return false;
    uint8_t r = 0, v[7], st = 0x0F, s;
    if (i2c_master_transmit_receive(dev, &st, 1, &s, 1, 100) != ESP_OK) return false;
    if (s & 0x80) {                                           /* OSF: dong ho tung dung (het pin) -> gio khong tin duoc */
        ESP_LOGW(TAG, "RTC mat gio (OSF): cho dong bo lai");
        return false;
    }
    if (i2c_master_transmit_receive(dev, &r, 1, v, 7, 100) != ESP_OK) return false;
    int sec = bcd(v[0] & 0x7F), min = bcd(v[1] & 0x7F), hr = bcd(v[2] & 0x3F);
    int d = bcd(v[4] & 0x3F), m = bcd(v[5] & 0x1F), y = 2000 + bcd(v[6]);
    if (v[2] & 0x40) return false;                            /* che do 12 gio: khong phai do minh ghi */
    if (sec > 59 || min > 59 || hr > 23 || d < 1 || d > 31 || m < 1 || m > 12 || y < 2025) return false;
    *unix_utc = days_from_civil(y, m, d) * 86400 + hr * 3600 + min * 60 + sec;
    return true;
}

bool ds3231_write(int64_t t)
{
    if (!dev || t < 0) return false;
    int y, m, d;
    civil_from_days(t / 86400, &y, &m, &d);
    int s = (int)(t % 86400);
    int wd = (int)((t / 86400 + 4) % 7) + 1;
    uint8_t b[8] = {0x00, tobcd(s % 60), tobcd(s / 60 % 60), tobcd(s / 3600), (uint8_t)wd, tobcd(d), tobcd(m), tobcd(y % 100)};
    if (i2c_master_transmit(dev, b, sizeof b, 100) != ESP_OK) return false;
    uint8_t st[2] = {0x0F, 0x00};                             /* xoa OSF */
    i2c_master_transmit(dev, st, 2, 100);
    return true;
}
