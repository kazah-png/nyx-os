#include "libc.h"

/* alarmdemo — timer-driven signals (v5.8.58). Installs a SIGALRM handler, arms
 * alarm(1), then sleeps; about a second later the kernel's scheduler tick posts
 * SIGALRM, which wakes the sleep and runs the handler on the syscall-return path.
 * Also checks the "seconds remaining" return of a re-armed alarm(). */

static volatile int fired = 0;

static void on_alarm(int sig) {
    (void)sig;
    fired = 1;
    printf("alarmdemo: SIGALRM delivered!\n");
}

int main(void) {
    signal(SIGALRM, on_alarm);

    /* alarm() returns the seconds left on a previous alarm. Arm 5s, then re-arm
     * 1s and confirm it reported ~5 remaining. */
    alarm(5);
    unsigned int prev = alarm(1);
    printf("alarmdemo: re-armed to 1s; previous alarm had ~%us left\n", prev);

    printf("alarmdemo: sleeping until SIGALRM fires...\n");
    for (int i = 0; i < 40 && !fired; i++) sleep_ms(100);   /* syscall returns deliver the signal */

    if (fired) {
        printf("alarmdemo: caught the alarm -- alarm()/SIGALRM works!\n");
        return 0;
    }
    printf("alarmdemo: alarm never fired :(\n");
    return 1;
}
