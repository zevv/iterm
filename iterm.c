
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <sys/select.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>
#include <curses.h>
#include <signal.h>
#include <errno.h>

#include "serial.h"

int local_echo = 0;

void usage(char *fname);
int handle_serial(int fd_serial, int fd_terminal);
int handle_terminal(int fd_terminal, int fd_serial);
void dump_printf(int color, char *fmt, ...);
void dump_putchar(int color, int c);
void create_windows(void);

#define COLOR_NORMAL		0
#define COLOR_TERMINAL		1
#define COLOR_SERIAL		2
#define COLOR_INFO		3

WINDOW *win;
WINDOW *win_bin = NULL;
WINDOW *win_sep = NULL;
WINDOW *win_hex = NULL;

void sigwinch(int data)
{
	endwin();
	refresh();
	create_windows();
}

int main(int argc, char **argv)
{
	struct timeval tv;
	fd_set fds;
	int fd_serial;
	int fd_terminal;
	int r;
	int o;
	int rtscts = 0;
	int baudrate = 9600;
	char ttydev[32] = "/dev/ttyS0";
	
	while( (o = getopt(argc, argv, "behrx")) != EOF) {
		switch(o) {
			case 'e':
				local_echo = 1;
				break;
			case 'r':
				rtscts=1;
				break;
		}
	}

	if(argc - optind < 2) {
		usage(argv[0]);
		exit(1);
	}
	
	strncpy(ttydev, argv[optind+0], sizeof(ttydev));
	baudrate = atoi(argv[optind+1]);

	fd_serial   = serial_open(ttydev, baudrate, rtscts);
	fd_terminal = 0;
	
	set_noncanonical(fd_serial, NULL);
	
	signal(SIGINT, SIG_IGN);

	signal(SIGWINCH, sigwinch);
	win = initscr();
	if(win == NULL) {
		printf("Can't create window\n");
		exit(1);
	}
	keypad(win, 1);
	noecho();
	start_color();

	create_windows();

	init_pair(COLOR_NORMAL,   COLOR_WHITE,   COLOR_BLACK);
	init_pair(COLOR_TERMINAL, COLOR_YELLOW,  COLOR_BLACK);
	init_pair(COLOR_SERIAL,   COLOR_GREEN,   COLOR_BLACK);
	init_pair(COLOR_INFO,     COLOR_CYAN,    COLOR_BLACK);

	while(1) {
	
		FD_ZERO(&fds);
		FD_SET(fd_serial, &fds);
		FD_SET(fd_terminal, &fds);
		
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		
		r = select(8, &fds, NULL, NULL, &tv);

		if(r > 0) {
			
			if(FD_ISSET(fd_serial, &fds)) {
			
				r = handle_serial(fd_serial, fd_terminal);
				if(r == -1) {
					goto cleanup;
				}
			}
			
			if(FD_ISSET(fd_terminal, &fds)) {
			
				r = handle_terminal(fd_terminal, fd_serial);
				if(r == -1) {
					goto cleanup;
				}
			}
			
		} else if(r<0) {
			
//			goto cleanup;
		}
		
	}

cleanup:	
	endwin();
	printf("Bye\n");
	return(0);
}


void create_windows(void)
{
	int h_total;
	int w_total;
	int h_bin;
	int h_sep;
	int h_hex;
	int y_bin;
	int y_sep;
	int y_hex;
	int i;

	getmaxyx(win, h_total, w_total);
	wresize(win, h_total, w_total);

	h_bin = h_total/2;
	h_sep = 1;
	h_hex = h_total - h_bin - 1;

	y_bin = 0;
	y_sep = h_bin;
	y_hex = h_bin+1;

	if(win_bin == NULL) { 
		win_bin = newwin(h_bin, w_total, y_bin, 0);
	} else {
		wresize(win_bin, h_bin, w_total);
		mvwin(win_bin, y_bin, 0);
		wrefresh(win_bin);
	}

	if(win_sep == NULL) {
		win_sep = newwin(h_sep, w_total, y_sep, 0);
	} else{
		wresize(win_sep, h_sep, w_total);
		mvwin(win_sep, y_sep, 0);
		wrefresh(win_sep);
	}

	if(win_hex == NULL) {
		win_hex = newwin(h_hex, w_total, y_hex, 0);
	} else {
		wresize(win_hex, h_hex, w_total);
		mvwin(win_hex, y_hex, 0);
		wrefresh(win_hex);
	}

	scrollok(win_bin, 1);
	scrollok(win_hex, 1);

	for(i=0; i<w_total; i++) waddch(win_sep, '-');
	wrefresh(win_sep);
}


int handle_serial(int fd_serial, int fd_terminal)
{
	char c;
	int len;
	
	len = read(fd_serial, &c, sizeof(c));
	if(len <= 0) return -1;

	dump_putchar(COLOR_SERIAL, c);

	return 0;
}


int handle_terminal(int fd_terminal, int fd_serial)
{
	char c;
	int len;
	static int in_hex;
	static int escape = 0;
	static int hexval = 0;

	len = read(fd_terminal, &c, 1);

	
	if(escape) {
	
		if(c == '~') {
			write(fd_serial, &c, 1);
			if(local_echo) dump_putchar(COLOR_TERMINAL, c);
		}
		
		else if(c == '.') {
			putchar('\n');
			return -1;
		}
	
		else if(c == 'c') {
			werase(win_bin);
			werase(win_hex);
			wrefresh(win_bin);
			wrefresh(win_hex);
		}

		else if(c == 'd') {
			serial_set_dtr(fd_serial, 0);
			dump_printf(COLOR_INFO, "<DTR hop>\n");
			sleep(1);
			serial_set_dtr(fd_serial, 1);
		}
		
		else if(c == 'x') {
			in_hex = 1;
		}
		
		else  {
			dump_printf(COLOR_INFO, 
				"\n"
				"  ~~ 	send tilde\n"
				"  ~.	exit\n"
				"  ~c	clear\n"
				"  ~d	dtr hop\n"
				"  ~xNN	enter HEX character\n"
				"\n" 
			);
		}
		
		escape = 0;
		
	} 
	
	else if(in_hex) {
	
		c = tolower(c);
		c = c - '0';
		if(c > 9) c -= 39;
		
		hexval = (hexval << 4) + c;

		if(++in_hex > 2) {
			write(fd_serial, &hexval, 1);
			if(local_echo) dump_putchar(COLOR_TERMINAL, hexval);
			in_hex = 0;
		}
		
	}
		
	else {
		if(c == '~') {
			escape = 1;
		} else {
			if(local_echo) dump_putchar(COLOR_TERMINAL, c);
			write(fd_serial, &c, 1);
		}
	}

	return 0;
}	


double hirestime(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return(tv.tv_sec + tv.tv_usec / 1000000.0);
}

void dump_printf(int color, char *fmt, ...)
{
	va_list va;
	char buf[256];
	int len;
	int i;

	va_start(va, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, va);
	va_end(va);

	for(i=0; i<len; i++) dump_putchar(color, buf[i]);
}


void dump_putchar(int color, int c)
{
	char buf[5];
	static double t_start = 0, t_now = 0, t_prev = 0;
	static int hexnum = 0;


	t_now =  hirestime();
	if(t_start == 0) t_start = t_now;
	if(t_prev  == 0) t_prev  = t_now;
	if(t_now - t_prev > 0.1) {
		hexnum = 0;
	}

	/*
	 * Print hex window
	 */

	wattrset(win_hex, A_NORMAL);
	if((hexnum % 16) == 0) {
		wprintw(win_hex, "\n%s", (hexnum == 0) ? "+" : " ");
		wprintw(win_hex, "%8.3f %8.3f   %04x: ", t_now-t_start, t_now-t_prev, hexnum);
	}
	if((hexnum % 16) == 8) wprintw(win_hex, "- ");
	
	wcolor_set(win_hex, color, NULL);
	wattron(win_hex, A_BOLD);
	snprintf(buf, sizeof(buf), "%02x ", (unsigned char)c);
	waddstr(win_hex, buf);
	wrefresh(win_hex);
	hexnum ++;

	/*
	 * Print bin window
	 */

	wcolor_set(win_bin, color, NULL);
	wattron(win_bin, A_BOLD);
	if(isprint(c) || isspace(c) || c==0x08) {
//		if(c == '\r') c = '\n';
		waddch(win_bin, c);
	} else {
		waddch(win_bin, '.');
	}
	wrefresh(win_bin);
	
	t_prev = t_now;
}


void usage(char *fname)
{
	printf("usage: %s [-r] <port> <baudrate\n", fname);
	printf("\n");
	printf("  -e	Local echo on\n");
	printf("  -r	use RTS/CTS hardware handshaking\n");
}


/*
 * End
 */
