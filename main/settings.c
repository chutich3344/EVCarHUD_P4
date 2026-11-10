#include <stdio.h>
#include <string.h>
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

/* Am luong rieng cua giong Mi Mi (tieng tit canh bao dung settings_get_volume) */
int settings_get_ai_volume(void) { int v = get_u8("aivol", 100); return v < 30 ? 30 : v > 100 ? 100 : v; }
void settings_set_ai_volume(int v) { set_u8("aivol", v < 30 ? 30 : v > 100 ? 100 : v); }

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

bool settings_get_last_night(void) { return get_u8("last_night", 1) != 0; }
void settings_set_last_night(bool night) { set_u8("last_night", night ? 1 : 0); }

bool settings_get_wifi_on(void) { return get_u8("wifi_on", 1) != 0; }
void settings_set_wifi_on(bool on) { set_u8("wifi_on", on ? 1 : 0); }

/* Nhieu mang da luu (toi da WIFI_SAVED_MAX), moi nhat dung dau: khoa "wf<i>s" / "wf<i>p".
 * Ban cu chi co "wifi_ssid" / "wifi_pass": doc lan dau thi chuyen sang danh sach. */
static int wifi_load(char ssid[][33], char pass[][65])
{
    nvs_handle_t h;
    int n = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    for (int i = 0; i < WIFI_SAVED_MAX; i++) {
        char ks[8], kp[8];
        size_t a = 33, b = 65;
        snprintf(ks, sizeof ks, "wf%ds", i);
        snprintf(kp, sizeof kp, "wf%dp", i);
        if (nvs_get_str(h, ks, ssid[n], &a) != ESP_OK || !ssid[n][0]) continue;
        if (nvs_get_str(h, kp, pass[n], &b) != ESP_OK) pass[n][0] = 0;
        n++;
    }
    if (!n) {
        size_t a = 33, b = 65;
        if (nvs_get_str(h, "wifi_ssid", ssid[0], &a) == ESP_OK && ssid[0][0]) {
            if (nvs_get_str(h, "wifi_pass", pass[0], &b) != ESP_OK) pass[0][0] = 0;
            n = 1;
        }
    }
    nvs_close(h);
    return n;
}

static void wifi_store(char ssid[][33], char pass[][65], int n)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    for (int i = 0; i < WIFI_SAVED_MAX; i++) {
        char ks[8], kp[8];
        snprintf(ks, sizeof ks, "wf%ds", i);
        snprintf(kp, sizeof kp, "wf%dp", i);
        if (i < n) {
            nvs_set_str(h, ks, ssid[i]);
            nvs_set_str(h, kp, pass[i]);
        } else {
            nvs_erase_key(h, ks);
            nvs_erase_key(h, kp);
        }
    }
    nvs_erase_key(h, "wifi_ssid");
    nvs_erase_key(h, "wifi_pass");
    nvs_commit(h);
    nvs_close(h);
}

int settings_wifi_list(char ssid[][33], char pass[][65])
{
    return wifi_load(ssid, pass);
}

bool settings_get_wifi(char ssid[33], char pass[65])
{
    char s[WIFI_SAVED_MAX][33], p[WIFI_SAVED_MAX][65];
    ssid[0] = pass[0] = 0;
    if (!wifi_load(s, p)) return false;
    memcpy(ssid, s[0], 33);
    memcpy(pass, p[0], 65);
    return true;
}

bool settings_wifi_find(const char *ssid, char pass[65])
{
    char s[WIFI_SAVED_MAX][33], p[WIFI_SAVED_MAX][65];
    int n = wifi_load(s, p);
    for (int i = 0; i < n; i++)
        if (!strcmp(s[i], ssid)) { memcpy(pass, p[i], 65); return true; }
    return false;
}

void settings_set_wifi(const char *ssid, const char *pass)       /* luu / cap nhat, dua len dau danh sach */
{
    char s[WIFI_SAVED_MAX + 1][33], p[WIFI_SAVED_MAX + 1][65];
    int n = wifi_load(s + 1, p + 1) + 1;
    snprintf(s[0], 33, "%s", ssid);
    snprintf(p[0], 65, "%s", pass ? pass : "");
    int k = 1;
    for (int i = 1; i < n; i++)
        if (strcmp(s[i], ssid)) { if (k != i) { memcpy(s[k], s[i], 33); memcpy(p[k], p[i], 65); } k++; }
    wifi_store(s, p, k > WIFI_SAVED_MAX ? WIFI_SAVED_MAX : k);
}

void settings_wifi_forget(const char *ssid)
{
    char s[WIFI_SAVED_MAX][33], p[WIFI_SAVED_MAX][65];
    int n = wifi_load(s, p), k = 0;
    for (int i = 0; i < n; i++)
        if (strcmp(s[i], ssid)) { if (k != i) { memcpy(s[k], s[i], 33); memcpy(p[k], p[i], 65); } k++; }
    wifi_store(s, p, k);
}
