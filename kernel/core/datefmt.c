// ============================================================
// datefmt.c - strftime(3)-subset formatter for the RTC clock (see datefmt.h).
// Pure logic, bounds-checked; the weekday/day-of-year come from the date.
// ============================================================
#include "datefmt.h"

static const char* const WD_FULL[7] = {
    "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday" };
static const char* const WD_ABBR[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
static const char* const MO_FULL[12] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December" };
static const char* const MO_ABBR[12] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };

// Sakamoto's algorithm: 0 = Sunday .. 6 = Saturday.
static int dt_dow(int y, int m, int d) {
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y -= 1;
    int w = (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
    return (w < 0) ? w + 7 : w;
}

static int dt_yday(int y, int m, int d) {
    static const int cum[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    int yd = cum[m-1] + d;
    if (m > 2 && leap) yd += 1;
    return yd;
}

static int put_s(char* out, int cap, int pos, const char* s) {
    while (*s && pos < cap - 1) out[pos++] = *s++;
    return pos;
}

// Right-justify v in a field of `width`, padded with `pad`. Bounded by cap.
static int put_num(char* out, int cap, int pos, int v, int width, char pad) {
    char tmp[12];
    int k = 0;
    if (v < 0) v = 0;
    if (v == 0) tmp[k++] = '0';
    while (v > 0 && k < (int)sizeof(tmp)) { tmp[k++] = (char)('0' + v % 10); v /= 10; }
    while (k < width && k < (int)sizeof(tmp)) tmp[k++] = pad;
    for (int i = k - 1; i >= 0 && pos < cap - 1; i--) out[pos++] = tmp[i];
    return pos;
}

int date_format(char* out, int cap, const char* fmt, const rtc_time_t* t) {
    if (!out || cap <= 0) return 0;
    int y = (int)t->year, mo = (int)t->month, d = (int)t->day;
    int h = (int)t->hour, mi = (int)t->minute, se = (int)t->second;
    if (mo < 1) mo = 1;
    if (mo > 12) mo = 12;                         // clamp so the name tables are safe
    int wd = dt_dow(y, mo, d);
    if (wd < 0 || wd > 6) wd = 0;
    int h12 = h % 12; if (h12 == 0) h12 = 12;

    int pos = 0;
    for (const char* p = fmt; *p && pos < cap - 1; p++) {
        if (*p != '%') { out[pos++] = *p; continue; }
        p++;
        if (!*p) { if (pos < cap - 1) out[pos++] = '%'; break; }
        switch (*p) {
            case 'Y': pos = put_num(out, cap, pos, y, 4, '0'); break;
            case 'y': pos = put_num(out, cap, pos, y % 100, 2, '0'); break;
            case 'm': pos = put_num(out, cap, pos, mo, 2, '0'); break;
            case 'd': pos = put_num(out, cap, pos, d, 2, '0'); break;
            case 'e': pos = put_num(out, cap, pos, d, 2, ' '); break;
            case 'H': pos = put_num(out, cap, pos, h, 2, '0'); break;
            case 'I': pos = put_num(out, cap, pos, h12, 2, '0'); break;
            case 'M': pos = put_num(out, cap, pos, mi, 2, '0'); break;
            case 'S': pos = put_num(out, cap, pos, se, 2, '0'); break;
            case 'j': pos = put_num(out, cap, pos, dt_yday(y, mo, d), 3, '0'); break;
            case 'p': pos = put_s(out, cap, pos, h < 12 ? "AM" : "PM"); break;
            case 'A': pos = put_s(out, cap, pos, WD_FULL[wd]); break;
            case 'a': pos = put_s(out, cap, pos, WD_ABBR[wd]); break;
            case 'B': pos = put_s(out, cap, pos, MO_FULL[mo-1]); break;
            case 'b': pos = put_s(out, cap, pos, MO_ABBR[mo-1]); break;
            case 'n': if (pos < cap - 1) out[pos++] = '\n'; break;
            case 't': if (pos < cap - 1) out[pos++] = '\t'; break;
            case '%': if (pos < cap - 1) out[pos++] = '%'; break;
            default:                              // unknown %X -> emit it literally
                if (pos < cap - 1) out[pos++] = '%';
                if (pos < cap - 1) out[pos++] = *p;
                break;
        }
    }
    out[pos] = '\0';
    return pos;
}

// ---- known-answer self-test (`datefmt`) ----
int datefmt_selftest(void) {
    rtc_time_t t;                                 // 2026-08-12 19:33:07 (a Wednesday)
    t.year = 2026; t.month = 8; t.day = 12;
    t.hour = 19; t.minute = 33; t.second = 7;
    struct { const char* fmt; const char* want; } c[] = {
        { "%Y-%m-%d",  "2026-08-12" },
        { "%H:%M:%S",  "19:33:07" },
        { "%A",        "Wednesday" },
        { "%a",        "Wed" },
        { "%B",        "August" },
        { "%b",        "Aug" },
        { "%I%p",      "07PM" },
        { "%y",        "26" },
        { "%j",        "224" },
        { "day %d%%",  "day 12%" },               // literal text + %% -> %
        { "%Q",        "%Q" },                    // unknown specifier kept literal
    };
    char buf[64];
    for (unsigned i = 0; i < sizeof(c)/sizeof(c[0]); i++) {
        int n = date_format(buf, sizeof(buf), c[i].fmt, &t);
        const char* w = c[i].want;
        int j = 0;
        while (buf[j] && w[j] && buf[j] == w[j]) j++;
        if (buf[j] != w[j]) return (int)(i + 1);
        int wl = 0; while (w[wl]) wl++;
        if (n != wl) return (int)(100 + i);       // returned length must match
    }
    return 0;
}
