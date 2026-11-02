/* HLP/1 (Waze HUD Link) - tach dong + giai ma JSON.
 * C thuan, khong phu thuoc LVGL/ESP-IDF: chay duoc tren PC va tren ESP32-P4. */
#ifndef HLP_H
#define HLP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HLP_MAX_FRAME  512   /* ca LF cuoi, tinh theo byte */
#define HLP_MAX_ALERTS 4
#define HLP_STR_MAX    128   /* 40 ky tu UTF-8 toi da ~ 120 byte */

/* ---------- tach dong (byte stream -> tung dong JSON) ---------- */
typedef void (*hlp_line_cb_t)(const char *line, size_t len, void *user);

typedef struct {
    char buf[HLP_MAX_FRAME];
    size_t len;
    bool overflow;
    uint32_t oversized;      /* so dong bi bo vi qua dai */
    hlp_line_cb_t cb;
    void *user;
} hlp_rx_t;

void hlp_rx_init(hlp_rx_t *rx, hlp_line_cb_t cb, void *user);
void hlp_rx_feed(hlp_rx_t *rx, const uint8_t *data, size_t len);

/* ---------- giai ma message ---------- */
typedef struct { int k, d, v, s, m; } hlp_alert_t;   /* d,v,s,m = -1 neu khong co */

typedef struct {
    int nav, spd, lim, over;
    int trn, trn2, dst, exit_n;           /* dst = -1: khong co */
    char st[HLP_STR_MAX], st2[HLP_STR_MAX];
    char eta[8];
    int rmin, rm;
    double rkm;
    int avg, avgL, avgR, avgP;
    int alr, alrD, alrV, alrS, alrM;      /* alrD/V/S/M = -1: khong co */
    hlp_alert_t alrs[HLP_MAX_ALERTS];
    int alrs_n;
    bool has_alrs;                        /* false: chua thuong luong hoac frame da bi cat */
    uint32_t ts;
} hlp_state_t;

typedef struct {
    int64_t sess;
    int64_t unix_time;
    int tz;                               /* phut lech so voi UTC */
    int rate;
    bool has_unix;
} hlp_hi_t;

typedef enum {
    HLP_MSG_UNKNOWN = 0,                  /* t la, bo qua theo quy tac tuong thich */
    HLP_MSG_STATE,
    HLP_MSG_HI,
    HLP_MSG_PING,
    HLP_MSG_PONG,
    HLP_MSG_BYE,
    HLP_MSG_ERROR
} hlp_msg_type_t;

typedef struct {
    hlp_msg_type_t type;
    hlp_state_t s;                        /* hop le khi type == HLP_MSG_STATE */
    hlp_hi_t hi;                          /* hop le khi type == HLP_MSG_HI   */
} hlp_msg_t;

/* Tra false neu dong hong hoac "v" khong phai 1. Key la bi bo qua, key thieu dung gia tri mac dinh. */
bool hlp_parse_line(const char *line, hlp_msg_t *out);

/* Bo dau tieng Viet de hien thi bang font khong co dau (vd "Nguyen Trai"). Ky tu la bi bo. */
void hlp_vn_ascii(const char *in, char *out, size_t cap);

/* Viet hoa gom ca chu Viet co dau ("Nguyễn Trãi" -> "NGUYỄN TRÃI"). Byte UTF-8 hong bi bo. */
void hlp_vn_upper(const char *in, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif
