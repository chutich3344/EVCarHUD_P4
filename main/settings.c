#include "settings.h"
#include "nvs.h"

#define NS "evcarhud"

static int get_u8(const char *key, int def)
{
    nvs_handle_t h;
    uint8_t v = (uint8_t)def;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return def;
    if (nvs_get_u8(h, key, &v) != ESP_OK) v = (uint8_t)def;
    nvs_close(h);
    return v;
}

static void set_u8(const char *key, int v)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, (uint8_t)v);
    nvs_commit(h);
    nvs_close(h);
}

int settings_get_mode(void) { return get_u8("mode", 0); }
void settings_set_mode(int mode) { set_u8("mode", mode); }
bool settings_get_demo(void) { return get_u8("demo", 0) != 0; }
void settings_set_demo(bool on) { set_u8("demo", on ? 1 : 0); }
bool settings_get_lcd_alt(void) { return get_u8("lcd", 0) != 0; }
void settings_set_lcd_alt(bool on) { set_u8("lcd", on ? 1 : 0); }
bool settings_get_rotated(void) { return get_u8("rot", 0) != 0; }   /* mac dinh khong xoay */
void settings_set_rotated(bool on) { set_u8("rot", on ? 1 : 0); }

bool settings_get_sound(void) { return get_u8("snd", 1) != 0; }
void settings_set_sound(bool on) { set_u8("snd", on ? 1 : 0); }
int settings_get_volume(void) { int v = get_u8("vol", 70); return v < 10 ? 10 : v > 100 ? 100 : v; }
void settings_set_volume(int v) { set_u8("vol", v); }

int settings_get_source(void) { return get_u8("src", 0) == 1 ? 1 : 0; }
void settings_set_source(int s) { set_u8("src", s == 1 ? 1 : 0); }

/* Gio lan cuoi lay tu dien thoai (giay UTC + mui gio phut): board khong co pin giu gio, bat may lai thi chay tiep tu day */
bool settings_get_clock(int64_t *unix_s, int *tz_min)
{
    nvs_handle_t h;
    int64_t t = 0;
    int16_t tz = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_i64(h, "clk", &t) == ESP_OK && nvs_get_i16(h, "tz", &tz) == ESP_OK && t > 0;
    nvs_close(h);
    if (ok) {
        *unix_s = t;
        *tz_min = tz;
    }
    return ok;
}

void settings_set_clock(int64_t unix_s, int tz_min)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i64(h, "clk", unix_s);
    nvs_set_i16(h, "tz", (int16_t)tz_min);
    nvs_commit(h);
    nvs_close(h);
}

int settings_get_tz(void)
{
    nvs_handle_t h;
    int16_t tz = 7 * 60;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return tz;
    if (nvs_get_i16(h, "tz", &tz) != ESP_OK || tz < -12 * 60 || tz > 14 * 60) tz = 7 * 60;
    nvs_close(h);
    return tz;
}

void settings_set_tz(int tz_min)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_i16(h, "tz", (int16_t)tz_min);
    nvs_commit(h);
    nvs_close(h);
}

bool settings_get_wifi_on(void) { return get_u8("wifi_on", 1) != 0; }
void settings_set_wifi_on(bool on) { set_u8("wifi_on", on ? 1 : 0); }

bool settings_get_wifi(char ssid[33], char pass[65])
{
    nvs_handle_t h;
    size_t a = 33, b = 65;
    ssid[0] = pass[0] = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = nvs_get_str(h, "wifi_ssid", ssid, &a) == ESP_OK && ssid[0];
    if (ok && nvs_get_str(h, "wifi_pass", pass, &b) != ESP_OK) pass[0] = 0;
    nvs_close(h);
    return ok;
}

void settings_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "wifi_ssid", ssid);
    nvs_set_str(h, "wifi_pass", pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
}
