/* 
 *	Includes 
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* 
 *	Defines 
 */

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
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

/* 
 *	Data 
 */

struct editorConfig {
	int cx, cy;
  int screenrows, screencols;
	struct termios orig_termios;
};

struct editorConfig E;

/*
 *	Terminal 
 */

/* Clear screen on exit */
void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

/* Change Terminal Mode back to default (Canonical) */
void disableRawMode() {
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

/* Change Terminal Mode to Raw Mode from default (Canonical)
 * Steps:
 * 	1) get current flags
 * 	2) alter current flags
 * 	3) set new flags
 * Flags turned off:
 * 	ECHO: Stop echoing
 * 	ICANON: read byte-by-byte instead of line-by-line
 * 	SIGINT/SIGSTP: stop CTRL-C/CTRL-Z kill signal
 * 	IXON: stop CTRL-S/CTRL-q start/stop signal
 * 	IEXTEN: stop CTRL-V wait signal
 * 	ICNRL: stop CTRL-M carriage return
 * 	OPOST: turn off output processing features
 * 	BRKINT/INPCK/ISTRIP/CS8/ISIG: ensure certain flags are off */
void enableRawMode() {
	if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcsetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_lflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_lflag &= ~(OPOST);
	raw.c_lflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tcsetattr"); 
}

/* Wait for one keypress and return it 
 * Deal with low-level terminal input */
int editorReadKey() {
	int nread;
	char c;
	while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if(nread == -1 && errno != EAGAIN)
			die("read");
	}

  if(c == '\x1b') {
    char seq[3];

    if(read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if(read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if(seq[0] == '[') {
      if(seq[1] >= '0' && seq[1] <= '9') {
        if(read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if(seq[2] == '~') {
          switch(seq[1]) {
            case '1':
              return HOME_KEY;
            case '3':
              return DEL_KEY;
            case '4':
              return END_KEY;
            case '5':
              return PAGE_UP;
            case '6':
              return PAGE_DOWN;
            case '7':
              return HOME_KEY;
            case '8':
              return END_KEY;
          }
        }
      } else {
        switch(seq[1]) {
          case 'A':
            return ARROW_UP;
          case 'B':
            return ARROW_DOWN;
          case 'C':
            return ARROW_RIGHT;
          case 'D':
            return ARROW_LEFT;
          case 'H':
            return HOME_KEY;
          case 'F':
            return END_KEY;
        }
      }
    } else if(seq[0] == 'O') {
      switch(seq[1]) {
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
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

  if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while(i <sizeof(buf) - 1) {
    if(read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if(buf[i] == 'R')
      break;
    
    i++;
  }

  buf[i] = '\0';

  if(buf[0] != '\x1b' || buf[1] != '[')
	  return -1;
  if(sscanf(&buf[2], "%d;%d", rows, cols) != 2)
	  return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;
	
	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1;
		return getCursorPosition(rows,cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*
 *  Append Buffer
 */

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

/* Allocate memory to append buffer */
void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if(new == NULL)
    return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

/* Free memory from append buffer */
void abFree(struct abuf *ab) {
  free(ab->b);
}

/*
 *	Output
 */

/* Draw each buffer row of the text being edited 
 * Clear lines one at a time: 3 bytes
 * K: 1 bytes, erases part of current line */ 
void editorDrawRows(struct abuf *ab) {
	int y;

	for(y = 0; y < E.screenrows; y++) {
		if(y == E.screenrows / 3) {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
      if(welcomelen > E.screencols) 
        welcomelen = E.screencols;
      int padding = (E.screencols - welcomelen) / 2;
      if(padding) {
        abAppend(ab, "~", 1);
        padding--;
      }
      while(padding--)
        abAppend(ab, " ", 1);
      abAppend(ab, welcome, welcomelen);
    } else {
      abAppend(ab, "~", 1);
    }

    abAppend(ab, "\x1b[K", 3);
    if(y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
	}
}

/* Escape sequence: <esc>[
 * \x1b: 1 bytes, escape character
 * Clear entire screen: 4 bytes
 * 	[: 1 bytes, escape sequence 
 * 	2J: 2 bytes, clear entire screen
 * Reposition the cursor: 3 bytes
 * 	H: cursor position 
 * Hide cursor when repainting: 6 bytes
 *  h/l: 1 bytes, set/reset mode
 *  ?25: 3 bytes, hide/show cursor */
void editorRefreshScreen() {
  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  //abAppend(&ab, "\x1b[2J", 4);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  //abAppend(&ab, "\x1b[H", 3);
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*
 *	Input
 */

void editorMoveCursor(int key) {
  switch(key) {
    case ARROW_LEFT:
      if(E.cx != 0) {
        E.cx--;
      }
      break;
    case ARROW_RIGHT:
      if(E.cx != E.screencols - 1) {
        E.cx++;
      }
      break;
    case ARROW_UP:
      if(E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if(E.cy != E.screenrows - 1) {
        E.cy++;
      }
      break;
  }
}

/* Wait for keypress, then handle it 
 * Map keys to higher level editor functions */
void editorProcessKeypress() {
  int c = editorReadKey();

  switch(c) {
    case CTRL_KEY('r'):
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);
      break;
    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:
      E.cx = E.screencols - 1;
      break;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = E.screenrows;
        while(times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;
	}
}

/*
 *	Initialization
 */

void initEditor() {
  E.cx = 0;
  E.cy = 0;

	if(getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
}

int main() {
	enableRawMode();
	initEditor();

	while(1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}
