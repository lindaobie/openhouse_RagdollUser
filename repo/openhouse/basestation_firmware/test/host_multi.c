/* Host test of the runtime round (multi.c): four stations, each its own copy of
 * multi.c loaded from a shared library, driven revolution by revolution the way the
 * timebase ISR drives it. Checks that no two stations ever sweep in one revolution,
 * while stations come and go and slaves miss parts of the master's frames, and that
 * the round settles on the stations present.
 *   usage: host_multi path/to/libstation.so */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void *h;
    void (*init)(int, uint32_t);
    void (*time)(uint32_t);
    int (*next_bit)(void);
    int (*count)(void);
    bool (*sweeps_in)(uint32_t);
    void (*superframe)(void);
    void (*master_sweep)(bool);
    void (*seen)(int, bool);
    void (*rev_start)(void);
    void (*master_bit)(int);
    void (*poll)(void);
    void (*task)(void);
    uint8_t (*mask_live)(void);
    bool (*mask_known)(void);
    uint32_t rev;
    bool on;
    bool sees_master;
} station_t;

static station_t st[4];

static void *sym(void *h, const char *n)
{
    void *p = dlsym(h, n);
    if (!p) { fprintf(stderr, "missing %s\n", n); exit(2); }
    return p;
}

static void load(station_t *s, const char *lib, int i)
{
    char path[512];
    snprintf(path, sizeof path, "/tmp/lhbs_station_%d_%d.so", getpid(), i);
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "cp '%s' '%s'", lib, path);
    if (system(cmd)) exit(2);
    s->h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!s->h) { fprintf(stderr, "%s\n", dlerror()); exit(2); }
    unlink(path);
    s->init = sym(s->h, "st_init");
    s->time = sym(s->h, "st_time");
    s->next_bit = sym(s->h, "st_next_bit");
    s->count = sym(s->h, "multi_count");
    s->sweeps_in = sym(s->h, "multi_sweeps_in");
    s->superframe = sym(s->h, "multi_superframe");
    s->master_sweep = sym(s->h, "multi_master_sweep");
    s->seen = sym(s->h, "multi_seen");
    s->rev_start = sym(s->h, "multi_rev_start");
    s->master_bit = sym(s->h, "multi_master_bit");
    s->poll = sym(s->h, "multi_poll");
    s->task = sym(s->h, "multi_task");
    s->mask_live = sym(s->h, "multi_mask_live");
    s->mask_known = sym(s->h, "multi_mask_known");
}

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* one revolution (1/60 s): each station's edge (rev++, superframe), its sweep, the
 * master's two flashes (OOTX bits to the slaves that see it), presence */
static int sweeps_total[4], collisions;
static double drop_bits = 0.0;   /* chance a slave misses a master flash */

static void revolution(uint32_t r)
{
    const uint32_t t = (uint32_t)(r * 1000ull / 60);
    for (int i = 0; i < 4; i++) {
        station_t *s = &st[i];
        if (!s->on) continue;
        s->time(t);
        s->rev++;
        s->rev_start();
        if (s->rev % (uint32_t)s->count() == 0) {
            int before = s->count();
            s->superframe();
            if (s->count() != before) s->rev = 0;
        }
    }
    /* the master's flashes come first in a revolution (slot 0, slaves 250 us apart);
     * a sweeping one tells the slaves where the superframe starts (corr_callback), and
     * each slave decides at its own index, after it */
    station_t *m = &st[0];
    const bool master_sweeps = m->sweeps_in(m->rev);
    for (int f = 0; f < 2; f++) {
        int bit = m->next_bit();
        for (int i = 1; i < 4; i++) {
            station_t *s = &st[i];
            if (!s->on || !s->sees_master) continue;
            if (drop_bits > 0 && rand() / (double)RAND_MAX < drop_bits) continue;
            s->seen(0, !master_sweeps);
            if (f == 0 && master_sweeps) {
                uint32_t c = (uint32_t)s->count();
                s->master_sweep(s->rev % c == 0);
                if (s->rev % c) s->rev -= s->rev % c;
            }
            s->master_bit(bit);
            s->poll();
        }
    }
    int sweeping = 0;
    for (int i = 0; i < 4; i++)
        if (st[i].on && st[i].sweeps_in(st[i].rev)) { sweeping++; sweeps_total[i]++; }
    if (sweeping > 1) {
        collisions++;
        if (collisions < 10) {
            printf("collision at rev %u:", r);
            for (int i = 0; i < 4; i++)
                if (st[i].on) printf("  [%d%s rev %u mask 0x%x known %d]", i + 1, st[i].sweeps_in(st[i].rev) ? " SWEEP" : "",
                                     st[i].rev, st[i].mask_live(), st[i].mask_known());
            printf("\n");
        }
    }
    for (int i = 1; i < 4; i++) if (st[i].on) m->seen(i, true);
    if (r % 6 == 0) for (int i = 0; i < 4; i++) if (st[i].on) st[i].task();
}

static void power(int i, bool on, uint32_t r)
{
    st[i].on = on;
    if (on) { st[i].init(i, (uint32_t)(r * 1000ull / 60)); st[i].rev = st[0].rev; }
}

static void run(uint32_t *r, uint32_t revs)
{
    for (uint32_t k = 0; k < revs; k++, (*r)++) revolution(*r);
}

static void expect_round(const char *what, uint8_t mask)
{
    int n = __builtin_popcount(mask);
    memset(sweeps_total, 0, sizeof sweeps_total);
    uint32_t r0 = 0;
    (void)r0;
    for (int i = 0; i < 4; i++)
        if (mask & (1u << i)) CHECK(st[i].mask_live() == mask, "%s: station %d's round is 0x%x, not 0x%x", what, i + 1, st[i].mask_live(), mask);
    (void)n;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s libstation.so\n", argv[0]); return 2; }
    for (int i = 0; i < 4; i++) { load(&st[i], argv[1], i); st[i].sees_master = true; }
    uint32_t r = 0;

    power(0, true, r);
    run(&r, 120);
    CHECK(st[0].count() == 1, "master alone: count %d", st[0].count());

    power(1, true, r); power(2, true, r);
    run(&r, 60 * 15);
    expect_round("slots 1-3 on", 0x7);
    memset(sweeps_total, 0, sizeof sweeps_total);
    run(&r, 600);
    for (int i = 0; i < 3; i++) CHECK(sweeps_total[i] >= 190 && sweeps_total[i] <= 210, "3 stations: station %d swept %d of 600", i + 1, sweeps_total[i]);

    power(3, true, r);
    run(&r, 60 * 15);
    expect_round("all four on", 0xF);
    memset(sweeps_total, 0, sizeof sweeps_total);
    run(&r, 600);
    for (int i = 0; i < 4; i++) CHECK(sweeps_total[i] >= 140 && sweeps_total[i] <= 160, "4 stations: station %d swept %d of 600", i + 1, sweeps_total[i]);

    /* slot 2 goes away: 1, 3, 4 share; station 4's turn moves from 4th to 3rd */
    power(1, false, r);
    run(&r, 60 * 15);
    expect_round("slot 2 off", 0xD);
    memset(sweeps_total, 0, sizeof sweeps_total);
    run(&r, 600);
    CHECK(sweeps_total[1] == 0, "slot 2 off still swept");
    for (int i = 0; i < 4; i++) if (i != 1) CHECK(sweeps_total[i] >= 190 && sweeps_total[i] <= 210, "slot 2 off: station %d swept %d of 600", i + 1, sweeps_total[i]);

    /* back on, with slaves missing a third of the master's flashes */
    drop_bits = 0.3;
    power(1, true, r);
    run(&r, 60 * 60);
    drop_bits = 0;
    run(&r, 60 * 10);
    expect_round("slot 2 back, lossy", 0xF);
    /* slaves missing a third of the master's flashes keep their turns: still locked,
     * the master seen moments ago, so they sweep without that revolution's flash */
    drop_bits = 0.3;
    memset(sweeps_total, 0, sizeof sweeps_total);
    run(&r, 600);
    drop_bits = 0;
    for (int i = 0; i < 4; i++) CHECK(sweeps_total[i] >= 140 && sweeps_total[i] <= 160, "lossy: station %d swept %d of 600", i + 1, sweeps_total[i]);

    /* a slave that can't see the master's data: never sweeps */
    st[3].sees_master = false;
    power(3, false, r); run(&r, 60 * 5);
    power(3, true, r);
    memset(sweeps_total, 0, sizeof sweeps_total);
    run(&r, 60 * 15);
    CHECK(sweeps_total[3] == 0, "a slave without the master's round swept %d times", sweeps_total[3]);

    /* stress: random power changes every few seconds, lossy, a long while */
    st[3].sees_master = true;
    srand(argc > 2 ? (unsigned)atoi(argv[2]) : 1);
    drop_bits = 0.2;
    for (int k = 0; k < 360; k++) {
        int i = 1 + rand() % 3;
        power(i, !st[i].on, r);
        run(&r, 60 * (2 + rand() % 8));
    }
    printf("collisions %d\n", collisions);
    CHECK(collisions == 0, "%d revolutions with two stations sweeping", collisions);
    printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures != 0;
}
