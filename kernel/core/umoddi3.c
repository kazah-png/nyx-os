// umoddi3.c - Rutinas de soporte para GCC (mínimo)
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned int uint32_t;

uint64_t __umoddi3(uint64_t a, uint64_t b) {
    // División simple por resta (no eficiente, pero funciona)
    if (b == 0) return 0;
    uint64_t rem = a;
    while (rem >= b) rem -= b;
    return rem;
}

uint64_t __udivdi3(uint64_t a, uint64_t b) {
    if (b == 0) return 0;
    uint64_t quot = 0;
    uint64_t rem = a;
    while (rem >= b) { rem -= b; quot++; }
    return quot;
}

int64_t __divdi3(int64_t a, int64_t b) {
    if (b == 0) return 0;
    int sign = 1;
    if (a < 0) { a = -a; sign = -sign; }
    if (b < 0) { b = -b; sign = -sign; }
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;
    uint64_t quot = 0;
    while (ua >= ub) { ua -= ub; quot++; }
    return sign * (int64_t)quot;
}