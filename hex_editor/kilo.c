/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/
#define KILO_VERSION "0.0.1"
#define KILO_QUIT_TIMES (3)
#define CTRL_KEY(k) ((k) & 0x1f)
#define LINE_LEN (0x100) // must bigger than 66 = 8(addr) + 1(":") + 40(hex) + 2(gap) + 16(ascii) 
#define ADDR_LEN (8)
#define LINE_CHARS (0x10)
#define GAP (4)
#define HEX_START_POS (ADDR_LEN+1)
#define HEX_END_POS (HEX_START_POS+LINE_CHARS/2*5 - 1)
#define ASCII_START_POS (HEX_END_POS + GAP + 1)
#define ASCII_END_POS (ASCII_START_POS + LINE_CHARS-1)
 

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_LEFT_BOUND,
    ARROW_RIGHT,
    ARROW_RIGHT_BOUND,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

typedef struct erow {
    int size;
    char *chars;
} erow;

struct editorConfig {
    int cx, cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios;
};

struct editorConfig E;

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorInsertRow(int at, char *s, size_t len);
void editorMoveCursor(int key);
static inline int hexToInt(char c);

/*** terminal ***/

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
               }
           }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    } else {
       return c;
    }
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);
 
    return -1;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        editorReadKey();
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
/*** format transform ***/

static inline int nCharHexPos(int n) {
    return HEX_START_POS + n/2*5 + 1 + (n%2)*2;
}

/*
 * getChar: return at'th character
 * @row: a pointer to target row ?
 * @at:  character index (0 - 15)
 */
static inline char getChar(erow *row, int at) {
    if (at < 0 || at > 15) return '\0';
    int hi = hexToInt(row->chars[nCharHexPos(at)]);
    int lo = hexToInt(row->chars[nCharHexPos(at)+1]);
    return hi*16+lo;
}

static inline bool isPrintable(char c) {
    return  c >= 0x20 && c < 0x7f;
}

static inline int hexToInt(char c) {
    return c >= 'a' ? c - 'a' + 10 : c - '0';
}
static int mappingPos(int p) {
    return p < ASCII_START_POS ? \
                             ASCII_START_POS + (p-ADDR_LEN-1)/5 * 2 + (((p-ADDR_LEN-1)%5)>2) : \
                             ADDR_LEN + 1 + (p-ASCII_START_POS)/2 * 5 + 1 + ((p-ASCII_START_POS)%2)*2;
}

static inline char intToHex(int d) {
    return d > 9 ? d%10 + 'a' : d + '0';
}

void toHexFormat(char *line, char *s, size_t len) {
    sprintf(line, "%.8x:", E.numrows*LINE_CHARS);
    for (unsigned int j = 0; j+1 < len; j+=2) {
        sprintf(line+strlen(line), " %02x%02x", (unsigned char)s[j], (unsigned char)s[j+1]);
    }
    
    if (len%2)
        sprintf(line+strlen(line), " %02x", (unsigned char)s[len-1]);

    int p = strlen(line);
    while (p < ASCII_START_POS) {
        line[p++] = ' ';
    }

    for (unsigned int j = 0; j < len; j++) {
        char c = s[j];
        if (!isPrintable(c))
            line[p++] = '.';
        else
            line[p++] = s[j];
    }
    line[p] = '\0';
}


/*
 * sendHexFormat: 
 * @s: input len , s[len] must be null.
 * @len: length of s, len == 0 means last line, then flush remaining.
 */
void sendHexFormat(char *s, size_t len) {
    if (s[len]) die("Invalid string");

    char line[LINE_LEN] = "";
    toHexFormat(line, s, len);
    editorInsertRow(E.numrows, line, strlen(line));
}

/*** row operations ***/

static void rowBackOne(erow *row, int at, unsigned char first) {
    int i, j;

    j = nCharHexPos(LINE_CHARS-1)+1;
    while(row->chars[j] == ' ') j--;
 
    if (j != HEX_END_POS) {
        i = j;
        j += (j-HEX_START_POS)%5 == 4 ? 3 : 2;
        row->size++;
    } else {
        i = j-2;
    }
    
    while (i>=at) {
        row->chars[j--] = row->chars[i--];
        row->chars[j--] = row->chars[i--];
        if (row->chars[i] == ' ') i--;
        if (row->chars[j] == ' ') j--;
    }

    for (i = ASCII_END_POS; i > mappingPos(at); i--)
        row->chars[i] = row->chars[i-1];

    row->chars[at] = intToHex(first/16);
    row->chars[at+1] = intToHex(first%16);
    row->chars[mappingPos(at)] = isPrintable(first) ? first : '.';
}

static void rowForwardOne(int r, int at, unsigned char last, bool final) {
    int i, j;

    if (at == HEX_START_POS+1) {
        E.row[r-1].chars[HEX_END_POS-1] = E.row[r].chars[at];
        E.row[r-1].chars[HEX_END_POS]   = E.row[r].chars[at+1];
    }
    else {
        int tmp = E.row[r].chars[at-1] == ' ' ? at-1 : at;
        E.row[r].chars[tmp-2] = E.row[r].chars[at];
        E.row[r].chars[tmp-1] = E.row[r].chars[at+1];
    }

    for (i = at, j = i+2; j <= HEX_END_POS;) {
        if (E.row[r].chars[i] == ' ') i++;
        if (E.row[r].chars[j] == ' ') j++;
        E.row[r].chars[i++] = E.row[r].chars[j++];
        E.row[r].chars[i++] = E.row[r].chars[j++];
    }   

    for (i = mappingPos(at); i < ASCII_END_POS; i++)
        E.row[r].chars[i] = E.row[r].chars[i+1];
        
    if (!final) {
        E.row[r].chars[HEX_END_POS-1] = intToHex(last/16);
        E.row[r].chars[HEX_END_POS] = intToHex(last%16);
        E.row[r].chars[ASCII_END_POS] = isPrintable(last) ? last : '.';
    }   
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;

    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
    
    E.row[at].size = len;
    E.row[at].chars = malloc(LINE_LEN+1);
    if (!E.row[at].chars) {
        die("malloc failed\n");
    }
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
    E.dirty++;
}

void editorFreeRow(erow *row) {
    free(row->chars);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty++;
}

void editorRowEditChar(erow *row, int at, unsigned char c) {
    char new_c;
    if (at < ASCII_START_POS) {
        row->chars[at] = c;
        if (((at-ADDR_LEN-1)%5) % 2)
            new_c = hexToInt(c)*16 + hexToInt(row->chars[at+1]);
        else
            new_c = hexToInt(row->chars[at-1])*16 + hexToInt(row->chars[at]);
        
       row->chars[mappingPos(at)] = isPrintable(new_c) ? new_c : '.';
    }
    else {
        row->chars[at] = c;
        char hi = intToHex(c/16);
        char lo = intToHex(c%16);
        row->chars[mappingPos(at)] = hi;
        row->chars[mappingPos(at)+1] = lo;
    }
}

void editorRowInsertChar(erow *row, int at) {
    if (at < 0 || at > ASCII_START_POS-GAP) return;
    if (!((at - HEX_START_POS)%5%2)) at--;

    bool spilling = false;
    char first, last;

    if (E.row[E.numrows-1].chars[HEX_END_POS] != ' ')
       spilling = true;

    last = getChar(row, 15);
    rowBackOne(row, at, 0);
    for (int i = E.cy+1; i < E.numrows; i++) {
        first = last;
	last = getChar(&E.row[i], 15);
        rowBackOne(&E.row[i], HEX_START_POS+1, first);
    }
    
    if (spilling) {
        char line[LINE_LEN] = "";
        char s[LINE_CHARS] = {last};
        toHexFormat(line, s, 1);
        editorInsertRow(E.numrows, line, strlen(line));
    }
 
    E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    E.dirty++;
}

void removeLastByte() {
    E.row[E.numrows-1].chars[HEX_END_POS] = ' ';
    E.row[E.numrows-1].chars[HEX_END_POS-1] = ' ';
    E.row[E.numrows-1].chars[ASCII_END_POS] = '\0';
}

void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at > ASCII_START_POS-GAP) return;
    
    if (row->chars[at] == ' ') at--;
    if (!((at - HEX_START_POS)%5%2)) at--;

    bool shrinking = false;
    char last;

    if (E.row[E.numrows-1].chars[nCharHexPos(1)] == ' ')
       shrinking = true;

    bool final = E.cy+1 == E.numrows;
    last = final ? '\0' : getChar(&E.row[E.cy+1], 0);
    rowForwardOne(E.cy, at, last, final);
    if (final)
        removeLastByte();

    for (int i = E.cy+1; i < E.numrows; i++) {
        final = i+1 == E.numrows;
        last = final ? '\0' : getChar(&E.row[i+1], 0);
        rowForwardOne(i, HEX_START_POS+1, last, i+1 == E.numrows);
        if (final)
            removeLastByte();
    }
    
    if (shrinking) {
        editorDelRow(E.numrows - 1);
        // special case
        if (E.cy == E.numrows) {
            E.cy--;
            E.cx = HEX_END_POS+2;
        }
    }
 
    E.dirty++;
}


/*** editor operations ***/

void editorEditChar(int c) {
    if (c < '0' || c > 'f' || (c > '9' && c < 'a')) return;
    editorRowEditChar(&E.row[E.cy], E.cx, c);
    if (E.cy+1 != E.numrows || E.row[E.cy].chars[E.cx+1] != ' ' || E.row[E.cy].chars[E.cx+2] != ' ')
        editorMoveCursor(ARROW_RIGHT_BOUND);
}

void editorInsertChar() {
    if (E.cx > HEX_END_POS) return;
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx);
    editorMoveCursor(ARROW_RIGHT_BOUND);
    editorMoveCursor(ARROW_RIGHT_BOUND);
}


void editorDelChar() {
    if (E.cy == E.numrows || E.cx > HEX_END_POS) return;
    if (E.cx < nCharHexPos(1) && E.cy == 0) return;
    editorRowDelChar(&E.row[E.cy], E.cx);
    
    if (E.cx == nCharHexPos(0)) {
        E.cy--;
        E.cx = HEX_END_POS+1;
    }
    editorMoveCursor(ARROW_LEFT_BOUND);
    editorMoveCursor(ARROW_LEFT_BOUND);
}

/*** file i/o ***/

char *editorRowToString(int *buflen) {
    int totlen = 0;
    int i, j;
    if (E.numrows == 0)
        return NULL;

    totlen = LINE_CHARS * E.numrows;
         
    char *buf = calloc(1, totlen+1);
    char *p = buf;
    for (i = 0; i < E.numrows; i++) {
        for(j = 0; j < LINE_CHARS; j++, p++)
            *p = getChar(&E.row[i], j);
    }

    p -= ASCII_END_POS - strlen(E.row[E.numrows-1].chars) + 1;
    *p = '\0';
    *buflen = p-buf;

    return buf;
}

static bool isEmpty(char *filename) {
    struct stat st;
    stat(filename, &st);
    return !(st.st_size > 0);
}

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    int fp = open(filename, O_RDONLY);
    if (fp<0 || isEmpty(filename))
        die("Invalid file: maybe not exist or file is empty");
     
    char line[LINE_CHARS+1] = "";
    int linelen;
    while ((linelen = read(fp, &line, LINE_CHARS))) {
        line[linelen] = '\0';
        sendHexFormat(line, linelen);
    }
    close(fp);
    E.dirty = 0;
}

void editorSave() {
    if (E.filename == NULL) {
        editorPrompt("Save as: %s", NULL);
        if (E.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
    }

    int len;
    char *buf = editorRowToString(&len);

    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.dirty = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/

void editorFindCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;

    if (key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) direction = 1;
    int current = last_match;
    int i;
    for (i = 0; i < E.numrows; i++) {
        current += direction;
        if (current == -1) current = E.numrows -1;
        else if (current == E.numrows) current = 0;

        erow *row = &E.row[current];
        char *match = strstr(row->chars, query);
        if (match) {
            last_match = current;
            E.cy = current;
            //E.cx = row.size; TBD
            E.rowoff = E.numrows;
            break;
        }
    }
}

void editorFind() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;

    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", NULL);

    if (query) { 
        free(query);
    } else {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
}

/*** append buffer ***/

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/

void editorScroll() {
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.cx < E.coloff) {
        E.coloff = E.cx;
    }
    if (E.cx >= E.coloff + E.screencols) {
        E.coloff = E.cx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].size - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].chars[E.coloff], len);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);
    
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** input ***/

char *editorPrompt(char *promp, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(promp, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) buf[--buflen] = '\0';
        } if (c == '\x1b') {
            editorSetStatusMessage("");
            if (callback) callback(buf, c);
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        if (callback) callback(buf, c);
   }
}

static bool isFinal(erow *row, int at) {
    if ((at == HEX_END_POS || at == ASCII_END_POS) ||
        (at < HEX_END_POS && row->chars[at+1] == ' ' && row->chars[at+2] == ' ') ||
        (at >= ASCII_START_POS && row->chars[at+1] == '\0'))
            return true; 

    return false;    
}

void editorMoveCursor(int key) {

    switch (key) {
        case ARROW_LEFT:
            if (E.cx == ASCII_START_POS) {
                E.cx = HEX_END_POS;
            }
            else if (E.cx == HEX_START_POS+1) {
                if (E.cy == 0) return;
                E.cy--;
                E.cx = ASCII_END_POS;
            }
            else {
                E.cx--;
                while (E.cx < HEX_END_POS && E.cx > HEX_START_POS && E.row[E.cy].chars[E.cx] == ' ' && E.row[E.cy].chars[E.cx-1] != ' ')
                    E.cx--;
            }
            break;

        case ARROW_RIGHT:
            if (E.cx == ASCII_END_POS) {
                if (E.cy + 1 == E.numrows) return;
                E.cy++;
                E.cx = HEX_START_POS + 1;
            }
            else if (E.cx == HEX_END_POS) {
                E.cx = ASCII_START_POS;
            }
            else {
                if (isFinal(&E.row[E.cy], E.cx)) return;
                E.cx++;
                while (E.cx < HEX_END_POS && E.row[E.cy].chars[E.cx] == ' ' && E.row[E.cy].chars[E.cx+1] != ' ')
                    E.cx++;
            }
            break;

        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;

        case ARROW_DOWN:
            if (E.cy != E.numrows-1) {
                E.cy++;
                while (E.cx <= HEX_END_POS && E.cx > HEX_START_POS && E.row[E.cy].chars[E.cx] == ' ')
                    E.cx--;
                while (E.cx <= ASCII_END_POS && E.cx > ASCII_START_POS && E.row[E.cy].chars[E.cx] == '\0')
                    E.cx--;
            }
            break;

        case ARROW_RIGHT_BOUND:
            if (E.cx == HEX_END_POS) {
                if (E.cy + 1 == E.numrows) return;
                E.cy++;
                E.cx = HEX_START_POS + 1;
            }
            else if (E.cx == ASCII_END_POS) {
                if (E.cy + 1 == E.numrows) return;
                E.cy++;
                E.cx = ASCII_START_POS;
            }
            else {
                if (isFinal(&E.row[E.cy], E.cx)) return;
                E.cx++;
                while (E.cx < HEX_END_POS && E.row[E.cy].chars[E.cx] == ' ' && E.row[E.cy].chars[E.cx+1] != ' ')
                    E.cx++;
                 
            }
            break;
        
        case ARROW_LEFT_BOUND:
            if (E.cx == HEX_START_POS+1) {
                if (E.cy == 0) return;
                E.cy--;
                E.cx = HEX_END_POS;
            }
            else if (E.cx == ASCII_START_POS) {
                if (E.cy == 0) return;
                E.cy--;
                E.cx = ASCII_END_POS;
            }
            else {
                E.cx--;
                while (E.cx < HEX_END_POS && E.cx > HEX_START_POS && E.row[E.cy].chars[E.cx] == ' ' && E.row[E.cy].chars[E.cx-1] != ' ')
                    E.cx--;
            }
            break;
    }

}

void editorProcessKeypress() {
    static int quit_times = KILO_QUIT_TIMES;

    int c = editorReadKey();

    switch (c) {
        case '\r':
            break;

        case CTRL_KEY('i'):
            editorInsertChar();
            break;

        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }

            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case CTRL_KEY('s'):
            editorSave();
            break;

        case HOME_KEY:
            E.cx = HEX_START_POS+1;
            break;

        case END_KEY:
            if (E.cy < E.numrows)
                E.cx = E.row[E.cy].size-1;
            break;

        case CTRL_KEY('f'):
            editorFind();
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN:
          {
            int times = E.screenrows;
            while (times--)
                editorMoveCursor( c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
          }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;

        default:
            editorEditChar(c);
            break;
    }

    quit_times = KILO_QUIT_TIMES;
}

/*** init ***/

void initEditor() {
    E.cx = ADDR_LEN+2;
    E.cy = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2;
}

int main (int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage ./kilo <file>\n");
        exit(1);
    }

    enableRawMode();
    initEditor();
    editorOpen(argv[1]);
    while (1) {
        editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find | Ctrl-I = Insert NULL");
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
