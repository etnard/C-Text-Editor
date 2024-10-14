#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void die(const char *s) {
	perror(s);
	exit(1);
}

void disableRawMode() { // terminal raw mode -> canonical mode
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode() { // terminal canonical mode -> raw mode
	if(tcgetattr(STDIN_FILENO, &orig_termios) == -1)
	       die("tcsetattr"); // get terminal attributes
	atexit(disableRawMode);

	struct termios raw = orig_termios;
	// Flags turned off:
	// ECHO: Stop echoing
	// ICANON: read byte-by-byte instead of line-by-line
	// SIGINT/SIGSTP: stop CTRL-C/CTRL-Z kill signal
	// IXON: stop CTRL-S/CTRL-q start/stop signal
	// IEXTEN: stop CTRL-V wait signal
	// ICNRL: stop CTRL-M carriage return
	// OPOST: turn off output processing features
	// Misc: ensure certain flags are off
	raw.c_lflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_lflag &= ~(OPOST);
	raw.c_lflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;


	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("tcsetattr"); // apply attributes to terminal
}

int main() {
	enableRawMode();

	while(1){
		char c = '\0';
		if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
			die("read");

		if(iscntrl(c)) { 
			printf("%d\r\n", c);
		} else {
			printf("%d ('%c')\r\n", c, c);
		}

		if(c == 'q')
			break;
	}

	return 0;
}
