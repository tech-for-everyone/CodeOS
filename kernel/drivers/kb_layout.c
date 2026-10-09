#include "kb_layout.h"
#include "../kernel/string.h"

/* ───── QWERTY (US) ───── */

static const uint8_t qwerty_normal[128] = {
     0,  0, '1','2','3','4','5','6','7','8','9','0','-','=', 0x08, 0,
    'q','w','e','r','t','y','u','i','o','p','[',']', 0x0A, 0,
    'a','s','d','f','g','h','j','k','l',';',0x27,'`', 0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' ', 0
};

static const uint8_t qwerty_shift[128] = {
     0,  0, '!','@','#','$','%','^','&','*','(',')','_','+',0x08, 0,
    'Q','W','E','R','T','Y','U','I','O','P','{','}', 0x0A, 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' ', 0
};

/* ───── AZERTY (French) ───── */

static const uint8_t azerty_normal[128] = {
     0,  0, '1','2','3','4','5','6','7','8','9','0',0x27,'=', 0x08, 0,
    'a','z','e','r','t','y','u','i','o','p','^','$', 0x0A, 0,
    'q','s','d','f','g','h','j','k','l','m','u','`', 0,
    '\\','w','x','c','v','b','n',',',';',':','!', 0,
    '*', 0, ' ', 0
};

static const uint8_t azerty_shift[128] = {
     0,  0, '1','2','3','4','5','6','7','8','9','0',0x27,'+', 0x08, 0,
    'A','Z','E','R','T','Y','U','I','O','P','*','\xa3', 0x0A, 0,
    'Q','S','D','F','G','H','J','K','L','M','%','\xb0', 0,
    '\\','W','X','C','V','B','N','?','.','/','\xa7', 0,
    '*', 0, ' ', 0
};

/* ───── QWERTZ (German) ───── */

static const uint8_t qwertz_normal[128] = {
     0,  0, '1','2','3','4','5','6','7','8','9','0',0x0df,'`', 0x08, 0,
    'q','w','e','r','t','z','u','i','o','p','[',']', 0x0A, 0,
    'a','s','d','f','g','h','j','k','l',';',0x27,'^', 0,
    '#','y','x','c','v','b','n','m',',','.','-', 0,
    '*', 0, ' ', 0
};

static const uint8_t qwertz_shift[128] = {
     0,  0, '!','"','#','$','%','&','/','(',')','=','?',0x27,'~', 0x08, 0,
    'Q','W','E','R','T','Z','U','I','O','P','{','}', 0x0A, 0,
    'A','S','D','F','G','H','J','K','L',':','"','\xb0', 0,
    '\\','Y','X','C','V','B','N','M',';',':','_', 0,
    '*', 0, ' ', 0
};

/* ───── DVORAK ───── */

static const uint8_t dvorak_normal[128] = {
     0,  0, '1','2','3','4','5','6','7','8','9','0','[',']', 0x08, 0,
    '\'',',','.','p','y','f','g','c','r','l','/','=', 0x0A, 0,
    'a','o','e','u','i','d','h','t','n','s','-','\\', 0,
    ';','q','j','k','x','b','m','w','v','z', 0,
    '*', 0, ' ', 0
};

static const uint8_t dvorak_shift[128] = {
     0,  0, '!','@','#','$','%','^','&','*','(',')','{','}', 0x08, 0,
    '"','<','>','P','Y','F','G','C','R','L','?','+', 0x0A, 0,
    'A','O','E','U','I','D','H','T','N','S','_','|', 0,
    ':','Q','J','K','X','B','M','W','V','Z', 0,
    '*', 0, ' ', 0
};

/* ───── COLEMAK ───── */

static const uint8_t colemak_normal[128] = {
     0,  0, '1','2','3','4','5','6','7','8','9','0','-','=', 0x08, 0,
    'q','w','f','p','g','j','l','u','y',';','[',']', 0x0A, 0,
    'a','r','s','t','d','h','n','e','i','o','\'','`', 0,
    '\\','z','x','c','v','b','k','m',',','.','/', 0,
    '*', 0, ' ', 0
};

static const uint8_t colemak_shift[128] = {
     0,  0, '!','@','#','$','%','^','&','*','(',')','_','+', 0x08, 0,
    'Q','W','F','P','G','J','L','U','Y',':','{','}', 0x0A, 0,
    'A','R','S','T','D','H','N','E','I','O','"','~', 0,
    '|','Z','X','C','V','B','K','M','<','>','?', 0,
    '*', 0, ' ', 0
};

/* ───── Layout table ───── */

static const kb_layout_t layouts[KB_LAYOUT_COUNT] = {
    { "qwerty",  qwerty_normal,  qwerty_shift  },
    { "azerty",  azerty_normal,  azerty_shift  },
    { "qwertz", qwertz_normal, qwertz_shift },
    { "dvorak",  dvorak_normal,  dvorak_shift  },
    { "colemak", colemak_normal, colemak_shift },
};

static kb_layout_id_t active_layout = KB_LAYOUT_QWERTY;

int kb_layout_set(kb_layout_id_t id) {
    if (id < 0 || id >= KB_LAYOUT_COUNT) return -1;
    active_layout = id;
    return 0;
}

const kb_layout_t *kb_layout_active(void) {
    return &layouts[active_layout];
}

kb_layout_id_t kb_layout_current(void) {
    return active_layout;
}

const kb_layout_t *kb_layout_get(kb_layout_id_t id) {
    if (id < 0 || id >= KB_LAYOUT_COUNT) return 0;
    return &layouts[id];
}

int kb_layout_find(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < KB_LAYOUT_COUNT; i++) {
        /* prefix match: "q" matches qwerty, "az" matches azerty */
        const char *l = layouts[i].name;
        int match = 1;
        for (int j = 0; name[j] && l[j]; j++) {
            if (name[j] != l[j] && name[j] != l[j] - 32) {
                match = 0; break;
            }
        }
        if (match) return i;
    }
    return -1;
}

const char *kb_layout_name(void) {
    return layouts[active_layout].name;
}
