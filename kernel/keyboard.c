// ============================================================
// keyboard.c - Controlador de teclado PS/2 (interrupt-driven)
// ============================================================
#include "kernel.h"
#include "spinlock.h"

// ------------------------------------------------------------
// Estados del teclado
// ------------------------------------------------------------
static int shift_pressed = 0;
static int altgr_pressed = 0;      // Alt derecho (AltGr)
static int caps_lock = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;        // Left Alt — tracked for window-management chords
static int e0_prefix = 0;          // Flag para el prefijo 0xE0

// ------------------------------------------------------------
// Buffer circular para caracteres (ISR -> getchar)
// ------------------------------------------------------------
#define KBD_BUFFER_SIZE 256
volatile char kbd_buffer[KBD_BUFFER_SIZE];
volatile int kbd_head = 0;
volatile int kbd_tail = 0;

static volatile int kbd_keycodes[KBD_BUFFER_SIZE];
static volatile int kbd_kc_head = 0;
static volatile int kbd_kc_tail = 0;

// Raw key-EVENT ring (v5.9.30 — the DOOM DG_GetKey enabler). Unlike the two rings
// above (cooked ASCII / press-only keycodes), this carries EVERY make AND break so a
// fullscreen app / game knows when a key is released. Codes are LAYOUT-INDEPENDENT
// Set-1 scancodes (physical key positions): bits 0-6 the scancode, bit 7 = E0-extended
// (arrows / nav). Packed event = (pressed << 8) | code. Filled by the IRQ handler and
// drained by keyboard_next_event() -> SYS_GETKEYEVENT, guarded by the same kbd_lock.
static volatile uint16_t kbd_events[KBD_BUFFER_SIZE];
static volatile int kbd_ev_head = 0;
static volatile int kbd_ev_tail = 0;
static int kbd_ev_e0 = 0;                  // E0 seen on the previous scancode byte

// SMP: both rings above (chars for stdin, keycodes for window management) and the
// modifier state machine inside scancode_to_ascii become shared the moment
// smpbalance spreads work off the BSP. The producer is the IRQ1 handler; consumers
// (getchar_poll, getkey_poll) can run on any AP, and getchar_poll's hardware-poll
// fallback re-enters scancode_to_ascii from a second context — so two cores could
// race the same head/tail (handing one keystroke to two readers, or corrupting an
// index). preempt_disable() never covered this: it means nothing to another core.
// One lock guards all of it — the leaf of the input path. Held only across the
// ring/state touch, never across a blocking wait (getchar hlts with it released) or
// signal delivery. (T1 SMP-locking track, v5.9.23.)
static spinlock_t kbd_lock = SPINLOCK_INIT;

// ------------------------------------------------------------
// Variable global de layout (definida aquí)
// ------------------------------------------------------------
int keyboard_layout = 1;           // 1 = Español por defecto

// ------------------------------------------------------------
// Tablas de mapeo para scancodes Set 2 (sin conversión)
// Los índices corresponden al scancode (0x00-0x7F)
// Para las teclas alfanuméricas, Set 2 y Set 1 comparten los mismos códigos.
// ------------------------------------------------------------

// --- Español (ES) ---
static char es_normal[0x80] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','\'',0xA1,'\b',
    '\t','q','w','e','r','t','y','u','i','o','p','`','+','\n',
    0,  'a','s','d','f','g','h','j','k','l',0xF1,0xB4,0xBA, 0,
    '<','z','x','c','v','b','n','m',',','.','-', 0, '*', 0, ' '
    // Remaining scancodes (0x40-0x7F) are zero-filled by C.
};

static char es_shift[0x80] = {
    0,   0,  '!','"',0xB7,'$','%','&','/','(',')','=','?',0xBF,'\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','^','*','\n',
    0,  'A','S','D','F','G','H','J','K','L',0xD1,0xA8,0xAA, 0,
    '>','Z','X','C','V','B','N','M',';',':','_', 0, '*', 0, ' '
};

static char es_altgr[0x80] = {
    0,   0,  '|','@','#','~',0x80,0xAC
};

// --- US (QWERTY) ---
static char us_normal[0x80] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`', 0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
};

static char us_shift[0x80] = {
    0,   0,  '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~', 0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
};

// ============================================================
// Inicialización
// ============================================================
void init_keyboard(void) {
    // Vaciar buffer de entrada
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    shift_pressed = 0;
    altgr_pressed = 0;
    ctrl_pressed = 0;
    alt_pressed = 0;
    caps_lock = 0;
    e0_prefix = 0;
}

// ============================================================
// Conversión de scancode a carácter
// ============================================================
// CALLER MUST HOLD kbd_lock: this updates the shared modifier state machine
// (shift/ctrl/alt/caps/altgr/e0) and enqueues into the keycode ring, and it runs
// from both the IRQ handler and getchar_poll's poll-direct fallback — i.e. from
// potentially two cores once smpbalance is on.
char scancode_to_ascii(uint8_t sc) {
    // --- 0xE0 prefix (extended keys) ---
    // Checked on the RAW byte, before the press-bit masking below: 0xE0 & 0x7F is
    // 0x60, so the old post-mask comparison could never match and extended keys
    // (arrows/home/end) never reached the keycode ring at all.
    if (sc == 0xE0) {
        e0_prefix = 1;
        return 0;
    }

    int pressed = !(sc & 0x80);
    sc &= 0x7F;

    if (e0_prefix) {
        e0_prefix = 0;
        if (sc == 0x38) {
            altgr_pressed = pressed;
            return 0;
        }
        if (sc == 0x1D) {
            ctrl_pressed = pressed;
            return 0;
        }
        // Map extended keys (Set 1 scancodes with E0 prefix)
        int keycode = 0;
        switch (sc & 0x7F) {
            case 0x48: keycode = KEY_UP;    break;
            case 0x50: keycode = KEY_DOWN;  break;
            case 0x4B: keycode = KEY_LEFT;  break;
            case 0x4D: keycode = KEY_RIGHT; break;
            case 0x47: keycode = KEY_HOME;  break;
            case 0x4F: keycode = KEY_END;   break;
            case 0x49: keycode = KEY_PGUP;  break;
            case 0x51: keycode = KEY_PGDN;  break;
            case 0x52: keycode = KEY_INSERT;break;
            case 0x53: keycode = KEY_DEL;   break;
        }
        if (keycode && pressed) {
            int next = (kbd_kc_head + 1) % KBD_BUFFER_SIZE;
            if (next != kbd_kc_tail) {
                kbd_keycodes[kbd_kc_head] = keycode;
                kbd_kc_head = next;
            }
        }
        return 0;
    }

    // --- Modificadores estándar (sin E0) ---
    if (sc == 0x2A || sc == 0x36) { // Shift izquierdo y derecho
        shift_pressed = pressed;
        return 0;
    }
    if (sc == 0x3A) { // Caps Lock
        if (pressed) caps_lock = !caps_lock;
        return 0;
    }
    if (sc == 0x38) { // Alt izquierdo (no AltGr)
        alt_pressed = pressed;
        return 0;
    }
    if (sc == 0x1D) { // Left Ctrl
        ctrl_pressed = pressed;
        return 0;
    }

    // Function keys F1-F12. These produce no character, so before this they were
    // simply dropped; the compositor needs F4 to reach it for the Alt+F4 close
    // chord, and routing the whole bank keeps the mapping uniform. Enqueued as
    // keycodes on the same ring as the arrows, press-edge only.
    {
        int fkey = 0;
        switch (sc) {
            case 0x3B: fkey = KEY_F1;  break;  case 0x3C: fkey = KEY_F2;  break;
            case 0x3D: fkey = KEY_F3;  break;  case 0x3E: fkey = KEY_F4;  break;
            case 0x3F: fkey = KEY_F5;  break;  case 0x40: fkey = KEY_F6;  break;
            case 0x41: fkey = KEY_F7;  break;  case 0x42: fkey = KEY_F8;  break;
            case 0x43: fkey = KEY_F9;  break;  case 0x44: fkey = KEY_F10; break;
            case 0x57: fkey = KEY_F11; break;  case 0x58: fkey = KEY_F12; break;
        }
        if (fkey) {
            if (pressed) {
                int next = (kbd_kc_head + 1) % KBD_BUFFER_SIZE;
                if (next != kbd_kc_tail) {
                    kbd_keycodes[kbd_kc_head] = fkey;
                    kbd_kc_head = next;
                }
            }
            return 0;
        }
    }

    // Si es una tecla liberada, no generar carácter
    if (!pressed) return 0;

    // --- Seleccionar tabla según layout ---
    char c = 0;
    if (keyboard_layout == 0) { // US
        if (shift_pressed || caps_lock)
            c = us_shift[sc];
        else
            c = us_normal[sc];
    } else { // Español
        if (altgr_pressed)
            c = es_altgr[sc];
        else if (shift_pressed || caps_lock)
            c = es_shift[sc];
        else
            c = es_normal[sc];
    }
    return c;
}

// ============================================================
// Manejador de interrupción del teclado (IRQ1)
// ============================================================
void keyboard_irq_handler(void* unused) {
    (void)unused;
    uint8_t st = inb(0x64);
    if ((st & 0x21) != 0x01) return;   // OBF=1, mouse bit=0 → keyboard data; else ignore
    uint8_t sc = inb(0x60);

    // Decode + buffer under kbd_lock so a consumer on another core can't race the
    // char ring or the modifier state. Ctrl-C/Ctrl-Z resolve to a signal delivered
    // AFTER the lock is released (signal_send_foreground walks the process table).
    int sig = 0;
    uint64_t fl = spin_lock_irqsave(&kbd_lock);

    // Raw key-event ring: record every make/break (with the E0 flag for extended
    // keys) so a fullscreen app polling SYS_GETKEYEVENT sees press AND release. This
    // is independent of the cooked char decode below — a game wants physical keys.
    if (sc == 0xE0) {
        kbd_ev_e0 = 1;                              // extended-key prefix; next byte is the key
    } else {
        uint16_t code = (uint16_t)((kbd_ev_e0 ? 0x80 : 0x00) | (sc & 0x7F));
        uint16_t ev   = (uint16_t)(((sc & 0x80) ? 0 : 0x100) | code);   // bit 8 = pressed
        kbd_ev_e0 = 0;
        int en = (kbd_ev_head + 1) % KBD_BUFFER_SIZE;
        if (en != kbd_ev_tail) { kbd_events[kbd_ev_head] = ev; kbd_ev_head = en; }
    }

    char c = scancode_to_ascii(sc);
    if (c) {
        if (ctrl_pressed && (c == 'c' || c == 'C')) {
            sig = SIGINT;                                   // don't buffer 'c'
        } else if (ctrl_pressed && (c == 'z' || c == 'Z')) {
            sig = SIGTSTP;                                  // don't buffer 'z'
        } else {
            // Ctrl+letter -> the ASCII control char (Ctrl-A=0x01 .. Ctrl-Z=0x1A), so
            // TUI programs can bind commands (nano-style Ctrl-O save / Ctrl-X exit).
            if (ctrl_pressed && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
                c = (char)((c | 0x20) - 'a' + 1);
            int next = (kbd_head + 1) % KBD_BUFFER_SIZE;
            if (next != kbd_tail) {
                kbd_buffer[kbd_head] = c;
                kbd_head = next;
            }
        }
    }
    spin_unlock_irqrestore(&kbd_lock, fl);

    if (sig) signal_send_foreground(sig);   // to the foreground process, lock released
}

// ============================================================
// Funciones de lectura
// ============================================================
static inline char kbd_poll_direct(void) {
    if (inb(0x64) & 0x01) {
        uint8_t sc = inb(0x60);
        return scancode_to_ascii(sc);
    }
    return 0;
}

// poll(): 1 if getchar_poll() would return a byte now (a buffered key, an i8042
// scancode, or serial input) without consuming it — for stdin (fd 0) readiness.
int keyboard_has_input(void) {
    uint64_t fl = spin_lock_irqsave(&kbd_lock);
    int ring = (kbd_tail != kbd_head);       // consistent snapshot of the software ring
    spin_unlock_irqrestore(&kbd_lock, fl);
    if (ring) return 1;
    if (inb(0x64) & 0x01) return 1;          // i8042 output buffer full (scancode waiting)
    if (inb(0x3FD) & 0x01) return 1;         // COM1 LSR bit 0: serial input available
    return 0;
}

char getchar_poll(void) {
    uint64_t fl = spin_lock_irqsave(&kbd_lock);
    if (kbd_tail != kbd_head) {
        char c = kbd_buffer[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
        spin_unlock_irqrestore(&kbd_lock, fl);
        return c;
    }
    // Ring empty: the hardware-poll fallback re-enters scancode_to_ascii (modifier
    // state + keycode ring), so it stays under the lock; serial touches no keyboard
    // state, so release first.
    char c = kbd_poll_direct();
    spin_unlock_irqrestore(&kbd_lock, fl);
    if (c) return c;
    c = serial_getchar_nonblock();
    if (c == 0x7F) return '\b';
    return c;
}

char getchar(void) {
    char c;
    while (1) {
        c = getchar_poll();
        if (c) return c;
        __asm__ volatile("hlt");
    }
}

// Returns extended keycodes (> 0x7F) from the keycode buffer,
// or ASCII chars from the standard buffer, or 0 if nothing.
int getkey_poll(void) {
    uint64_t fl = spin_lock_irqsave(&kbd_lock);
    if (kbd_kc_tail != kbd_kc_head) {
        int kc = kbd_keycodes[kbd_kc_tail];
        kbd_kc_tail = (kbd_kc_tail + 1) % KBD_BUFFER_SIZE;
        spin_unlock_irqrestore(&kbd_lock, fl);
        return kc;
    }
    spin_unlock_irqrestore(&kbd_lock, fl);   // release before getchar_poll re-locks
    char c = getchar_poll();
    if (c) return c;
    return 0;
}

// SYS_GETKEYEVENT: pop the next raw key event, or -1 if the ring is empty. The value
// is (pressed << 8) | code, where code is a Set-1 scancode with bit 7 flagging an
// E0-extended (arrow / nav) key. Non-blocking — a fullscreen app polls it each frame.
int keyboard_next_event(void) {
    uint64_t fl = spin_lock_irqsave(&kbd_lock);
    int r = -1;
    if (kbd_ev_tail != kbd_ev_head) {
        r = (int)kbd_events[kbd_ev_tail];
        kbd_ev_tail = (kbd_ev_tail + 1) % KBD_BUFFER_SIZE;
    }
    spin_unlock_irqrestore(&kbd_lock, fl);
    return r;
}

// ============================================================
// Función para cambiar de layout
// ============================================================
void set_keyboard_layout(int layout) {
    if (layout == 0 || layout == 1) {
        keyboard_layout = layout;
    }
}

int is_ctrl_pressed(void) {
    return ctrl_pressed;
}

int is_alt_pressed(void) {
    return alt_pressed;
}
