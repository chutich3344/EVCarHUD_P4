#include "hlp.h"
#include <stdlib.h>
#include <string.h>

/* ====================== TACH DONG ====================== */
void hlp_rx_init(hlp_rx_t *rx, hlp_line_cb_t cb, void *user)
{
    memset(rx, 0, sizeof *rx);
    rx->cb = cb;
    rx->user = user;
}

void hlp_rx_feed(hlp_rx_t *rx, const uint8_t *data, size_t len)
{
    if (!rx || !data) return;
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            if (rx->overflow) {
                rx->oversized++;                       /* bo dong qua dai, dong bo lai o LF nay */
            } else {
                size_t n = rx->len;
                if (n && rx->buf[n - 1] == '\r') n--;  /* xoa \r truoc khi them NUL */
                rx->buf[n] = 0;
                if (n && rx->cb) rx->cb(rx->buf, n, rx->user);
            }
            rx->len = 0;
            rx->overflow = false;
        } else if (!rx->overflow) {
            if (rx->len >= HLP_MAX_FRAME - 1) rx->overflow = true;   /* 511 byte + LF = 512 */
            else rx->buf[rx->len++] = c;
        }
    }
}

/* ====================== JSON TOI GIAN ====================== */
typedef struct { const char *p; } cur_t;

static void ws(cur_t *c)
{
    while (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n') c->p++;
}

static int hex4(const char *p)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char h = p[i];
        v <<= 4;
        if (h >= '0' && h <= '9') v |= h - '0';
        else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
        else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
        else return -1;
    }
    return v;
}

static size_t put_utf8(char *o, size_t cap, size_t at, unsigned cp)
{
    char tmp[4];
    size_t n;
    if (cp < 0x80) { tmp[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { tmp[0] = (char)(0xC0 | (cp >> 6)); tmp[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) { tmp[0] = (char)(0xE0 | (cp >> 12)); tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
    else { tmp[0] = (char)(0xF0 | (cp >> 18)); tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
    if (o && at + n < cap) { memcpy(o + at, tmp, n); return at + n; }
    return at;                                         /* day buffer: bo ky tu, khong cat giua chung */
}

/* Doc chuoi vao out (cap byte, co NUL). out == NULL: chi bo qua. */
static bool parse_string(cur_t *c, char *out, size_t cap)
{
    if (*c->p != '"') return false;
    c->p++;
    size_t at = 0;
    bool full = false;                                  /* da day: bo phan con lai, khong cat giua ky tu */
    while (*c->p && *c->p != '"') {
        unsigned char ch = (unsigned char)*c->p;
        if (ch == '\\') {
            c->p++;
            char e = *c->p;
            if (!e) return false;
            c->p++;
            unsigned cp;
            switch (e) {
            case 'n': cp = '\n'; break;
            case 't': cp = '\t'; break;
            case 'r': cp = '\r'; break;
            case 'b': cp = '\b'; break;
            case 'f': cp = '\f'; break;
            case 'u': {
                int h = hex4(c->p);
                if (h < 0) return false;
                c->p += 4;
                cp = (unsigned)h;
                if (cp >= 0xD800 && cp <= 0xDBFF && c->p[0] == '\\' && c->p[1] == 'u') {
                    int lo = hex4(c->p + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        c->p += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + ((unsigned)lo - 0xDC00);
                    }
                }
                break;
            }
            default: cp = (unsigned char)e; break;     /* \" \\ \/ */
            }
            if (!full) {
                size_t before = at;
                at = put_utf8(out, cap, at, cp);
                if (at == before) full = true;
            }
        } else {
            size_t n = ch < 0x80 ? 1 : ch >= 0xF0 ? 4 : ch >= 0xE0 ? 3 : ch >= 0xC0 ? 2 : 1;
            for (size_t k = 1; k < n; k++)                /* byte tiep theo phai la 10xxxxxx, neu khong coi nhu 1 byte */
                if (((unsigned char)c->p[k] & 0xC0) != 0x80) { n = 1; break; }
            if (out && !full && at + n < cap) { memcpy(out + at, c->p, n); at += n; }
            else full = true;
            c->p += n;
        }
    }
    if (*c->p != '"') return false;
    c->p++;
    if (out && cap) out[at < cap ? at : cap - 1] = 0;
    return true;
}

static bool parse_number(cur_t *c, double *out)
{
    char *end;
    double v = strtod(c->p, &end);
    if (end == c->p) return false;
    c->p = end;
    if (out) *out = v;
    return true;
}

static bool skip_value(cur_t *c, int depth)
{
    if (depth > 8) return false;
    ws(c);
    char ch = *c->p;
    if (ch == '"') return parse_string(c, NULL, 0);
    if (ch == '{' || ch == '[') {
        char close = ch == '{' ? '}' : ']';
        c->p++;
        ws(c);
        if (*c->p == close) { c->p++; return true; }
        for (;;) {
            ws(c);
            if (ch == '{') {
                if (!parse_string(c, NULL, 0)) return false;
                ws(c);
                if (*c->p++ != ':') return false;
            }
            if (!skip_value(c, depth + 1)) return false;
            ws(c);
            if (*c->p == ',') { c->p++; continue; }
            if (*c->p == close) { c->p++; return true; }
            return false;
        }
    }
    if (!strncmp(c->p, "true", 4))  { c->p += 4; return true; }
    if (!strncmp(c->p, "false", 5)) { c->p += 5; return true; }
    if (!strncmp(c->p, "null", 4))  { c->p += 4; return true; }
    return parse_number(c, NULL);
}

/* alrs: [{"k":2,"d":300,"v":40,...}, ...] */
static bool parse_alrs(cur_t *c, hlp_state_t *s)
{
    ws(c);
    if (*c->p != '[') return skip_value(c, 0);
    c->p++;
    s->has_alrs = true;
    s->alrs_n = 0;
    ws(c);
    if (*c->p == ']') { c->p++; return true; }
    for (;;) {
        ws(c);
        if (*c->p != '{') return false;
        c->p++;
        hlp_alert_t a = {0, -1, -1, -1, -1};
        ws(c);
        if (*c->p != '}') {
            for (;;) {
                char key[8];
                ws(c);
                if (!parse_string(c, key, sizeof key)) return false;
                ws(c);
                if (*c->p++ != ':') return false;
                ws(c);
                double v;
                if (!strcmp(key, "k") || !strcmp(key, "d") || !strcmp(key, "v") ||
                    !strcmp(key, "s") || !strcmp(key, "m")) {
                    if (!parse_number(c, &v)) return false;
                    if (key[0] == 'k') a.k = (int)v;
                    else if (key[0] == 'd') a.d = (int)v;
                    else if (key[0] == 'v') a.v = (int)v;
                    else if (key[0] == 's') a.s = (int)v;
                    else a.m = (int)v;
                } else if (!skip_value(c, 1)) {
                    return false;
                }
                ws(c);
                if (*c->p == ',') { c->p++; continue; }
                if (*c->p == '}') break;
                return false;
            }
        }
        c->p++;                                        /* '}' */
        if (s->alrs_n < HLP_MAX_ALERTS) s->alrs[s->alrs_n++] = a;
        ws(c);
        if (*c->p == ',') { c->p++; continue; }
        if (*c->p == ']') { c->p++; return true; }
        return false;
    }
}

bool hlp_parse_line(const char *line, hlp_msg_t *out)
{
    if (!line || !out) return false;
    memset(out, 0, sizeof *out);
    hlp_state_t *s = &out->s;
    s->dst = -1; s->alrD = -1; s->alrV = -1; s->alrS = -1; s->alrM = -1;

    cur_t c = {line};
    char type[12] = "";
    int version = 0;

    ws(&c);
    if (*c.p++ != '{') return false;
    ws(&c);
    if (*c.p == '}') return false;                      /* khong co v/t */

    for (;;) {
        char key[12];
        ws(&c);
        if (!parse_string(&c, key, sizeof key)) return false;
        ws(&c);
        if (*c.p++ != ':') return false;
        ws(&c);

        double v;
        const char *k = key;
        if (!strcmp(k, "t")) {
            if (!parse_string(&c, type, sizeof type)) return false;
        } else if (!strcmp(k, "st")) {
            if (!parse_string(&c, s->st, sizeof s->st)) return false;
        } else if (!strcmp(k, "st2")) {
            if (!parse_string(&c, s->st2, sizeof s->st2)) return false;
        } else if (!strcmp(k, "eta")) {
            if (!parse_string(&c, s->eta, sizeof s->eta)) return false;
        } else if (!strcmp(k, "alrs")) {
            if (!parse_alrs(&c, s)) return false;
        } else {
            struct { const char *name; int *dst; } ints[] = {
                {"v", &version}, {"nav", &s->nav}, {"spd", &s->spd}, {"lim", &s->lim},
                {"over", &s->over}, {"trn", &s->trn}, {"trn2", &s->trn2}, {"dst", &s->dst},
                {"exit", &s->exit_n}, {"rmin", &s->rmin}, {"rm", &s->rm}, {"avg", &s->avg},
                {"avgL", &s->avgL}, {"avgR", &s->avgR}, {"avgP", &s->avgP}, {"alr", &s->alr},
                {"alrD", &s->alrD}, {"alrV", &s->alrV}, {"alrS", &s->alrS}, {"alrM", &s->alrM},
                {"tz", &out->hi.tz}, {"rate", &out->hi.rate},
            };
            bool done = false;
            for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) {
                if (!strcmp(k, ints[i].name)) {
                    if (*c.p == '"' || *c.p == '{' || *c.p == '[') {   /* sai kieu: bo qua gia tri */
                        if (!skip_value(&c, 0)) return false;
                    } else {
                        if (!parse_number(&c, &v)) return false;
                        *ints[i].dst = (int)v;
                    }
                    done = true;
                    break;
                }
            }
            if (!done) {
                if (!strcmp(k, "rkm") || !strcmp(k, "ts") || !strcmp(k, "sess") || !strcmp(k, "unix")) {
                    if (*c.p == '"' || *c.p == '{' || *c.p == '[') {
                        if (!skip_value(&c, 0)) return false;
                    } else {
                        if (!parse_number(&c, &v)) return false;
                        if (!strcmp(k, "rkm")) s->rkm = v;
                        else if (!strcmp(k, "ts")) s->ts = (uint32_t)v;
                        else if (!strcmp(k, "sess")) out->hi.sess = (int64_t)v;
                        else { out->hi.unix_time = (int64_t)v; out->hi.has_unix = true; }
                    }
                } else if (!skip_value(&c, 0)) {                         /* key la: bo qua */
                    return false;
                }
            }
        }

        ws(&c);
        if (*c.p == ',') { c.p++; continue; }
        if (*c.p == '}') break;
        return false;
    }

    if (version != 1) return false;                     /* v khac 1: bo qua ca message */
    if (!strcmp(type, "s")) out->type = HLP_MSG_STATE;
    else if (!strcmp(type, "hi")) out->type = HLP_MSG_HI;
    else if (!strcmp(type, "ping")) out->type = HLP_MSG_PING;
    else if (!strcmp(type, "pong")) out->type = HLP_MSG_PONG;
    else if (!strcmp(type, "bye")) out->type = HLP_MSG_BYE;
    else if (!strcmp(type, "error")) out->type = HLP_MSG_ERROR;
    else out->type = HLP_MSG_UNKNOWN;
    return true;
}

/* ====================== BO DAU TIENG VIET ====================== */
static const struct { char base; const char *chars; } VN[] = {
    {'a', "àáạảãâầấậẩẫăằắặẳẵ"}, {'A', "ÀÁẠẢÃÂẦẤẬẨẪĂẰẮẶẲẴ"},
    {'e', "èéẹẻẽêềếệểễ"},       {'E', "ÈÉẸẺẼÊỀẾỆỂỄ"},
    {'i', "ìíịỉĩ"},             {'I', "ÌÍỊỈĨ"},
    {'o', "òóọỏõôồốộổỗơờớợởỡ"}, {'O', "ÒÓỌỎÕÔỒỐỘỔỖƠỜỚỢỞỠ"},
    {'u', "ùúụủũưừứựửữ"},       {'U', "ÙÚỤỦŨƯỪỨỰỬỮ"},
    {'y', "ỳýỵỷỹ"},             {'Y', "ỲÝỴỶỸ"},
    {'d', "đ"},                 {'D', "Đ"},
};

static size_t u8len(unsigned char ch)
{
    return ch < 0x80 ? 1 : ch >= 0xF0 ? 4 : ch >= 0xE0 ? 3 : ch >= 0xC0 ? 2 : 1;
}

void hlp_vn_ascii(const char *in, char *out, size_t cap)
{
    size_t at = 0;
    if (!out || !cap) return;
    while (in && *in && at + 1 < cap) {
        unsigned char ch = (unsigned char)*in;
        size_t n = u8len(ch);
        for (size_t k = 1; k < n; k++)
            if (!in[k]) { n = 1; break; }
        if (n == 1) {
            if (ch < 0x80) out[at++] = (char)ch;        /* ASCII; byte tiep noi la thi bo */
            in++;
            continue;
        }
        if (ch == 0xCC || (ch == 0xCD && (unsigned char)in[1] <= 0xAF)) {   /* dau roi U+0300..U+036F */
            in += n;
            continue;
        }
        char base = 0;
        for (size_t v = 0; v < sizeof VN / sizeof VN[0] && !base; v++)
            for (const char *p = VN[v].chars; *p; p += u8len((unsigned char)*p))
                if (u8len((unsigned char)*p) == n && !memcmp(p, in, n)) { base = VN[v].base; break; }
        if (base) out[at++] = base;
        in += n;
    }
    out[at] = 0;
}

/* ====================== VIET HOA CO DAU ====================== */
static size_t get_utf8(const char *s, unsigned *cp)           /* so byte da doc; 0 = het chuoi */
{
    unsigned char c = (unsigned char)s[0];
    if (!c) return 0;
    size_t n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    for (size_t k = 1; k < n; k++)
        if (((unsigned char)s[k] & 0xC0) != 0x80) { n = 1; break; }
    if (n == 1) { *cp = c < 0x80 ? c : 0; return 1; }         /* byte la: cp = 0 -> bo qua */
    if (n == 2) *cp = ((c & 0x1Fu) << 6) | ((unsigned char)s[1] & 0x3Fu);
    else if (n == 3) *cp = ((c & 0x0Fu) << 12) | (((unsigned char)s[1] & 0x3Fu) << 6) | ((unsigned char)s[2] & 0x3Fu);
    else *cp = ((c & 0x07u) << 18) | (((unsigned char)s[1] & 0x3Fu) << 12) | (((unsigned char)s[2] & 0x3Fu) << 6) | ((unsigned char)s[3] & 0x3Fu);
    return n;
}

static unsigned cp_upper(unsigned cp)
{
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) return cp - 32;                 /* a-grave .. thorn */
    if (cp == 0x103 || cp == 0x111 || cp == 0x129 || cp == 0x169) return cp - 1; /* ă đ ĩ ũ */
    if (cp == 0x1A1 || cp == 0x1B0) return cp - 1;                              /* ơ ư */
    if (cp >= 0x1EA1 && cp <= 0x1EF9 && (cp & 1)) return cp - 1;                /* Latin Extended Additional */
    return cp;
}

void hlp_vn_upper(const char *in, char *out, size_t cap)
{
    size_t at = 0;
    if (!out || !cap) return;
    while (in && *in) {
        unsigned cp = 0;
        size_t n = get_utf8(in, &cp);
        if (!n) break;
        in += n;
        if (!cp) continue;
        size_t before = at;
        at = put_utf8(out, cap, at, cp_upper(cp));
        if (at == before) break;                              /* day buffer */
    }
    out[at < cap ? at : cap - 1] = 0;
}
