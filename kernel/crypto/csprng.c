// ============================================================
// csprng.c - HMAC_DRBG-SHA256 CSPRNG (NIST SP 800-90A), v5.9.57
// ============================================================
// Until now the TLS ClientHello random and the ephemeral X25519 private key came from a
// plain LCG — predictable, so a passive observer could recompute the "secret" key and
// decrypt the session. This replaces that with a proper HMAC_DRBG (SP 800-90A) over the
// existing HMAC-SHA256, seeded from hardware entropy: RDSEED then RDRAND when the CPU
// exposes them (detected via CPUID), always mixed with RDTSC jitter and the tick counter.
// The DRBG math is deterministic and pinned by a NIST-format known-answer test (the
// `csprngtest` command); the entropy sources feed the live instance only.
#include "../core/kernel.h"
#include "csprng.h"
#include "sha256.h"

// ---- HMAC_DRBG (SP 800-90A) ---------------------------------------------------------

typedef struct { uint8_t K[32]; uint8_t V[32]; uint32_t reseed_ctr; } hmac_drbg_t;

// (K, V) = HMAC_DRBG_Update(provided_data, K, V)
static void drbg_update(hmac_drbg_t* d, const uint8_t* data, uint32_t len) {
    uint8_t buf[32 + 1 + 160], t[32];
    if (len > 160) len = 160;                         // our seeds are small; bound the scratch

    memcpy(buf, d->V, 32); buf[32] = 0x00;            // K = HMAC(K, V || 0x00 || data)
    if (len) memcpy(buf + 33, data, len);
    hmac_sha256(d->K, 32, buf, 33 + len, t); memcpy(d->K, t, 32);
    hmac_sha256(d->K, 32, d->V, 32, t);   memcpy(d->V, t, 32);   // V = HMAC(K, V)

    if (len) {
        memcpy(buf, d->V, 32); buf[32] = 0x01;        // K = HMAC(K, V || 0x01 || data)
        memcpy(buf + 33, data, len);
        hmac_sha256(d->K, 32, buf, 33 + len, t); memcpy(d->K, t, 32);
        hmac_sha256(d->K, 32, d->V, 32, t);   memcpy(d->V, t, 32);
    }
}

static void drbg_init(hmac_drbg_t* d, const uint8_t* seed, uint32_t len) {
    for (int i = 0; i < 32; i++) { d->K[i] = 0x00; d->V[i] = 0x01; }
    d->reseed_ctr = 1;
    drbg_update(d, seed, len);
}

static void drbg_generate(hmac_drbg_t* d, uint8_t* out, uint32_t n,
                          const uint8_t* addl, uint32_t alen) {
    if (alen) drbg_update(d, addl, alen);
    uint8_t t[32];
    uint32_t done = 0;
    while (done < n) {
        hmac_sha256(d->K, 32, d->V, 32, t); memcpy(d->V, t, 32);   // V = HMAC(K, V)
        uint32_t take = n - done; if (take > 32) take = 32;
        memcpy(out + done, d->V, take); done += take;
    }
    drbg_update(d, addl, alen);
    d->reseed_ctr++;
}

// ---- hardware entropy ---------------------------------------------------------------

static void cpuid(uint32_t leaf, uint32_t sub,
                  uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* dd) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*dd) : "a"(leaf), "c"(sub));
}
static int cpu_has_rdrand(void) { uint32_t a,b,c,d; cpuid(1,0,&a,&b,&c,&d); return (c >> 30) & 1; }
static int cpu_has_rdseed(void) { uint32_t a,b,c,d; cpuid(7,0,&a,&b,&c,&d); return (b >> 18) & 1; }

static int rdrand64(uint64_t* o) {
    unsigned char ok;
    __asm__ volatile("rdrand %0\n\tsetc %1" : "=r"(*o), "=qm"(ok) :: "cc");
    return ok;
}
static int rdseed64(uint64_t* o) {
    unsigned char ok;
    __asm__ volatile("rdseed %0\n\tsetc %1" : "=r"(*o), "=qm"(ok) :: "cc");
    return ok;
}
static uint64_t rd_tsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

// Fill buf[0..n) (n a multiple of 8) with entropy: a hardware sample per 8 bytes when the
// CPU has RDSEED/RDRAND, always XOR-folded with RDTSC + the tick counter so there is a live
// (if weak) source even on a CPU without a hardware RNG. The DRBG whitens whatever we feed.
static uint32_t gather_entropy(uint8_t* buf, uint32_t n) {
    int have_rd = cpu_has_rdrand();
    int have_sd = cpu_has_rdseed();
    uint32_t i = 0;
    while (i + 8 <= n) {
        uint64_t v = 0; int got = 0;
        if (have_sd) for (int t = 0; t < 32 && !got; t++) got = rdseed64(&v);
        if (!got && have_rd) for (int t = 0; t < 32 && !got; t++) got = rdrand64(&v);
        v ^= rd_tsc();
        v ^= ((uint64_t)get_ticks() << 24);
        v ^= (uint64_t)(i + 1) * 0x9E3779B97F4A7C15ULL;
        for (int b = 0; b < 8; b++) buf[i++] = (uint8_t)(v >> (8 * b));
    }
    return i;
}

// ---- public CSPRNG ------------------------------------------------------------------

static hmac_drbg_t g_drbg;
static int g_ready = 0;

void csprng_bytes(uint8_t* out, uint32_t n) {
    if (!g_ready) {
        uint8_t seed[48];
        uint32_t sl = gather_entropy(seed, sizeof(seed));
        drbg_init(&g_drbg, seed, sl);
        g_ready = 1;
    } else if ((g_drbg.reseed_ctr & 0x3F) == 0) {   // periodic reseed with fresh entropy
        uint8_t seed[24];
        uint32_t sl = gather_entropy(seed, sizeof(seed));
        drbg_update(&g_drbg, seed, sl);
    }
    uint64_t stir = rd_tsc();                        // stir in a fresh timing sample each call
    drbg_generate(&g_drbg, out, n, (const uint8_t*)&stir, sizeof(stir));
}

// ---- known-answer self-test ---------------------------------------------------------

static int hv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static void unhex(uint8_t* out, const char* h, int n) {
    for (int i = 0; i < n; i++) out[i] = (uint8_t)((hv(h[2*i]) << 4) | hv(h[2*i+1]));
}

int csprng_selftest(void) {
    // NIST SP 800-90A HMAC_DRBG-SHA256 (no reseed / no PR / no personalization / no additional
    // input): instantiate from EntropyInput || Nonce, discard the first Generate, and the second
    // Generate reproduces the reference bits (cross-checked against an independent HMAC_DRBG).
    uint8_t seed[48], out1[128], out2[128], want[128];
    unhex(seed, "ca851911349384bffe89de1cbdc46e6831e44d34a4fb935ee285dd14b71a7488"
                "659ba96c601dc69fc902940805ec0ca8", 48);
    unhex(want, "e528e9abf2dece54d47c7e75e5fe302149f817ea9fb4bee6f4199697d04d5b89"
                "d54fbb978a15b5c443c9ec21036d2460b6f73ebad0dc2aba6e624abf07745bc1"
                "07694bb7547bb0995f70de25d6b29e2d3011bb19d27676c07162c8b5ccde0668"
                "961df86803482cb37ed6d5c0bb8d50cf1f50d476aa0458bdaba806f48be9dcb8", 128);

    hmac_drbg_t d;
    drbg_init(&d, seed, 48);
    drbg_generate(&d, out1, 128, 0, 0);              // first Generate — discarded
    drbg_generate(&d, out2, 128, 0, 0);              // second Generate — the returned bits

    int ok = 1;
    for (int i = 0; i < 128; i++) if (out2[i] != want[i]) ok = 0;

    if (ok) printf("csprng: HMAC_DRBG(SHA-256) NIST known-answer vector PASS\n");
    else    printf("csprng: HMAC_DRBG(SHA-256) NIST known-answer vector FAIL\n");
    printf("csprng: hardware entropy — RDRAND=%s, RDSEED=%s (RDTSC + ticks always mixed in)\n",
           cpu_has_rdrand() ? "yes" : "no", cpu_has_rdseed() ? "yes" : "no");
    printf("csprng: self-test %d/1 passed\n", ok ? 1 : 0);
    return ok ? 0 : -1;
}
