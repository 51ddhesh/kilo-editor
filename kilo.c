// kilo.c - A small, simple text editor in C
// Inspired by kilo by Salvatore Sanfilippo (antirez)
// This file implements a terminal-based text editor with basic features:
// - Open, edit, and save text files
// - Cursor movement, scrolling, and simple search
// - Status and message bars
// - Handles raw terminal input and escape sequences
//
// * INCLUDES ----------
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// * DEFINES ---------
// KILO_VERSION: Editor version string
#define KILO_VERSION "0.0.1"
// KILO_TAB_STOP: Number of spaces per tab
#define KILO_TAB_STOP 8 
// KILO_QUIT_TIMES: Times to press Ctrl-Q to quit with unsaved changes
#define KILO_QUIT_TIMES 3
// CTRL_KEY: Macro to get control-key value
#define CTRL_KEY(k) ((k) & 0x1f)

// editorKey: Enum for special key codes (arrows, delete, etc.)
enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

// * DATA ----------
// erow: Structure for a single line of text (with rendered version for tabs)
typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

// editorConfig: Main editor state/configuration
struct editorConfig {
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    int dirty_flag;
    char *filename;
    char statusmsg[80];
    time_t status_time;
    struct termios orig_termios;
};
struct editorConfig E;

// * PROTOTYPES ----------
// Function declarations for status, screen refresh, and prompt
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));


// * TERMINAL ----------
// die: Exit with error, clear screen
void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    
    perror(s);
    exit(1);
}

// disableRawMode: Restore terminal to original mode
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

// enableRawMode: Set terminal to raw mode for direct input
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

// editorReadKey: Read a keypress, handle escape sequences for special keys
int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    if (c == '\x1b') {
        // Handle escape sequences for special keys (arrows, home, end, etc.)
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            // CSI sequences: ESC [ ...
            if (seq[1] >= '0' && seq[1] <= '9') {
                // Extended escape sequence: ESC [ <num> ~
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
                // Simple arrow/home/end: ESC [ A/B/C/D/H/F
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
            // ESC O H/F (alternative home/end)
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b'; // Unrecognized escape sequence
    } else {
        return c; // Normal character
    }
}

// getCursorPosition: Query terminal for cursor position
int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;
    // Request cursor position report
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    // Read response: ESC [ rows ; cols R
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    // Parse response
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

// getWindowSize: Get terminal window size (rows, cols)
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    // Try ioctl first
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // Fallback: move cursor to bottom-right and query position
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

// * ROW OPERATIONS ----------
// Functions to manipulate lines (rows): insert, delete, update, etc.

int editorRowRxToCx(erow *row, int rx) {
    int current_rx = 0;
    int cx;
    for (cx = 0; cx < row -> size; cx++) {
        if (row -> chars[cx] == '\t') 
            current_rx += (KILO_TAB_STOP - 1) - (current_rx % KILO_TAB_STOP);
        current_rx++;
        if (current_rx > rx) return cx;
    }
    return cx;
}

int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        if (row -> chars[j] == '\t') 
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

// editorUpdateRow: Update the rendered version of a row (expand tabs)
void editorUpdateRow(erow *row) {
    int tabs = 0;
    // Count number of tabs in the row
    for (int i = 0; i < row -> size; i++) {
        if (row -> chars[i] == '\t') tabs++;
    }
    // Allocate enough space for expanded tabs
    free(row -> render);
    row -> render = malloc(row -> size + (tabs * (KILO_TAB_STOP - 1)) + 1);
    int idx = 0;
    for (int i = 0; i < row -> size; i++) {
        if (row -> chars[i] == '\t') {
            // Insert a space, then pad with spaces to next tab stop
            row -> render[idx++] = ' ';
            while (idx % KILO_TAB_STOP != 0) row -> render[idx++] = ' ';
        } else {
            row -> render[idx++] = row -> chars[i];
        }
    }
    row -> render[idx] = '\0';
    row -> rsize = idx;
}

// editorInsertRow: Insert a new row at position 'at'
void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > E.numrows) return;
    // Make space for new row
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
    // Initialize new row
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);
    E.numrows++;
    E.dirty_flag++;
}

void editorFreeRow(erow *row) {
    free(row -> render);
    free(row -> chars);
}

// editorDelRow: Delete a row at position 'at'
void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    // Free memory for row
    editorFreeRow(&E.row[at]);
    // Shift rows up
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--;
    E.dirty_flag++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row -> size) at = row -> size;
    row -> chars = realloc(row -> chars, row -> size + 2);
    memmove(&row -> chars[at + 1], &row -> chars[at], row -> size - at + 1);
    row -> size++;
    row -> chars[at] = c;
    editorUpdateRow(row);
    E.dirty_flag++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row -> chars = realloc(row -> chars, row -> size + len + 1);
    memcpy(&row -> chars[row -> size], s, len);
    row -> size += len;
    row -> chars[row -> size] = '\0';
    editorUpdateRow(row);
    E.dirty_flag++; 
}

void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row -> size) return;
    memmove(&row -> chars[at], &row -> chars[at + 1], row -> size - at);
    row -> size--;
    editorUpdateRow(row);
    E.dirty_flag++;
}

// * EDITOR OPERATIONS ----------
// Functions for editing: insert char, new line, delete char

void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

void editorInsertNewLine() {
    if (E.cx == 0) {
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorDelChar() {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;
    erow *row = &E.row[E.cy];
    if (E.cx > 0) { 
        editorRowDelChar(row, E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row -> chars, row -> size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

// * FILE I/O ----------
// Functions to load and save files

char *editorRowsToString(int *buflen) {
    int total_len = 0;
    for (int i = 0; i < E.numrows; i++) {
        total_len += E.row[i].size + 1;
    }
    *buflen = total_len;
    char *buf = malloc(total_len);
    char *p = buf;
    for (int i = 0; i < E.numrows; i++) {
        memcpy(p, E.row[i].chars, E.row[i].size);
        p += E.row[i].size;
        *p = '\n';
        p++;
    }
    return buf;
}

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
        editorInsertRow(E.numrows, line, linelen);
    }
    free(line);
    fclose(fp);
    E.dirty_flag = 0;
}

void editorSave() {
    if (E.filename == NULL) {
        E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
        if (E.filename == NULL) {
            editorSetStatusMessage("Save aborted");
            return;
        }
    }
    int len;
    char *buf = editorRowsToString(&len);
    int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                E.dirty_flag = 0;
                editorSetStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Cant Save! I/O error: %s", strerror(errno));
}

// * FIND ----------
// Simple search functionality with callback for interactive search

void editorFindCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;
    // Handle navigation keys and reset on new search
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
    // Search through all rows in the given direction
    for (int i = 0; i < E.numrows; i++) {
        current += direction;
        if (current == -1) current = E.numrows - 1;
        else if (current == E.numrows) current = 0;
        erow *row = &E.row[current];
        char *match = strstr(row -> render, query);
        if (match) {
            // Found a match: update cursor and state
            last_match = current;
            E.cy = current;
            E.cx = editorRowRxToCx(row, match - row -> render);
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

    char *query = editorPrompt("Search: %s (USE ESC/Arrows/Enter)", editorFindCallback);
    if (query) free(query);
    else {
        E.cx = saved_cx;
        E.cy = saved_cy;
        E.coloff = saved_coloff;
        E.rowoff = saved_rowoff;
    }
}

// * APPEND BUFFER ----------
// abuf: Dynamic string buffer for efficient screen drawing
struct abuf {
    char *b;
    int len;
};
#define ABUF_INIT {NULL, 0}

// abAppend: Append string to abuf
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}
// abFree: Free abuf memory
void abFree(struct abuf *ab) {
    free(ab->b);
}

// * OUTPUT ----------
// Functions to draw rows, status bar, message bar, and refresh the screen

void editorScroll() {
    E.rx = 0;
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

void editorDrawRows(struct abuf *ab) {
  for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                "Kilo editor -- version %s", KILO_VERSION);
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
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);  // Inverted colors
    
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines", 
                      E.filename ? E.filename : "[No Name]", E.numrows, E.dirty_flag ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
    
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);
    
    while (len < E.screencols) {  // Fixed: E.screencols instead of E.screenrows
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    
    abAppend(ab, "\x1b[m", 3);  // Restore normal formatting
    abAppend(ab, "\r\n", 2);    // Add a new line after the status bar
}

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.status_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
    editorScroll();
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // Hide cursor
    abAppend(&ab, "\x1b[H", 3);    // Move to top-left to start drawing

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    // Move cursor to updated position
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // Show cursor again

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.status_time = time(NULL);
}


// * INPUT ----------
// editorPrompt: Prompt user for input (used for save-as, search, etc.)
// editorMoveCursor: Handle cursor movement for arrow keys
// editorProcessKeypress: Main input handler (dispatches on key pressed)

char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        // Handle backspace/delete
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) buf[--buflen] = '\0';
        } else if (c == '\x1b') {
            // Cancel input
            editorSetStatusMessage("");
            if (callback) callback(buf, c);
            free(buf);
            return NULL;
        } else if (c == '\r') {
            // Enter: accept input if not empty
            if (buflen != 0) {
                editorSetStatusMessage("");
                if (callback) callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            // Append normal character
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

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
        break;
        case ARROW_RIGHT:
            if (row && E.cx < row -> size) {
                E.cx++;
            } else if (row && E.cx == row -> size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row -> size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}


void editorProcessKeypress() {
    static int quit_times = KILO_QUIT_TIMES;
    int c = editorReadKey();
    switch (c) {
        case '\r':
            editorInsertNewLine();
            break;

        case CTRL_KEY('q'):
            if (E.dirty_flag && quit_times > 0) {
                editorSetStatusMessage("WARNING! File has unsaved changes. ""Press Ctrl-Q %d more time(s) to quit.", quit_times);
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
            E.cx = 0;
            break;
        case END_KEY:
            if (E.cy < E.numrows) 
                E.cx = E.row[E.cy].size;
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
        case PAGE_DOWN: {
            if (c == PAGE_UP) {
                E.cy = E.rowoff;
            } else if (c == PAGE_DOWN) {
                E.cy = E.rowoff + E.screenrows - 1;
                if (E.cy > E.numrows) E.cy = E.numrows;
            }
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
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
            editorInsertChar(c);
            break;
    }

    quit_times = KILO_QUIT_TIMES;
}

// * INIT ----------
// initEditor: Initialize editor state
// main: Entry point, main loop

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty_flag = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.status_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    char c;
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
