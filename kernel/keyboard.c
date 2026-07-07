// ============================================================
// keyboard.c - Controlador de teclado PS/2 (interrupt-driven)
// ============================================================
#include "kernel.h"

// ------------------------------------------------------------
// Estados del teclado
// ------------------------------------------------------------
static int shift_pressed = 0;
static int altgr_pressed = 0;      // Alt derecho (AltGr)
static int caps_lock = 0;
static int ctrl_pressed = 0;
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
    caps_lock = 0;
    e0_prefix = 0;
}

// ============================================================
// Conversión de scancode a carácter
// ============================================================
char scancode_to_ascii(uint8_t sc) {
    int pressed = !(sc & 0x80);
    sc &= 0x7F;

    // --- Prefijo 0xE0 (teclas extendidas) ---
    if (sc == 0xE0) {
        e0_prefix = 1;
        return 0;
    }

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
        return 0;
    }
    if (sc == 0x1D) { // Left Ctrl
        ctrl_pressed = pressed;
        return 0;
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
    if ((st & 0x21) == 0x01) {  // OBF=1, mouse bit=0 → keyboard data
        uint8_t sc = inb(0x60);
        char c = scancode_to_ascii(sc);
        if (c) {
            if (ctrl_pressed && (c == 'c' || c == 'C')) {   // Ctrl-C -> SIGINT
                signal_send_foreground(SIGINT);             // to the foreground process
                return;                                     // consume it (don't buffer 'c')
            }
            int next = (kbd_head + 1) % KBD_BUFFER_SIZE;
            if (next != kbd_tail) {
                kbd_buffer[kbd_head] = c;
                kbd_head = next;
            }
        }
    }
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

char getchar_poll(void) {
    if (kbd_tail != kbd_head) {
        char c = kbd_buffer[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
        return c;
    }
    char c = kbd_poll_direct();
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
    if (kbd_kc_tail != kbd_kc_head) {
        int kc = kbd_keycodes[kbd_kc_tail];
        kbd_kc_tail = (kbd_kc_tail + 1) % KBD_BUFFER_SIZE;
        return kc;
    }
    char c = getchar_poll();
    if (c) return c;
    return 0;
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
