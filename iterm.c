
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <ctype.h>

#include "serial.h"
#include "mainloop.h"

static int fd_serial;
static int fd_terminal;
static int hex_off = 0;
static int hex_mode = 0;
static int translate_newline = 0;
static char hex_buf[90] = "";
static int timestamp = 0;
static int echo = 0;
static int have_tty;

static int get_baudrate(const char *s);
static int on_terminal_read(int fd, void *data);
static int on_serial_read(int fd, void *data);
static int on_status_timer(void *data);
static void msg(const char *fmt, ...);
static void usage(char *fname);
static int on_sigint(int signo, void *data);
static void set_hex_mode(int onoff);
static void terminal_write(uint8_t c, int local);

int main(int argc, char **argv)
{
	struct termios save;
	int o;
	int rtscts = 0;
	int baudrate = 115200;
	char ttydev[32] = "/dev/ttyS0";
	
	have_tty = isatty(1);
	
	while( (o = getopt(argc, argv, "b:ehnrt")) != EOF) {
		switch(o) {
			case 'e':
				echo = 1;
				break;
			case 'b':
				baudrate = get_baudrate(optarg);
				break;
			case 'n':
				translate_newline ++;
				break;
			case 'r':
				rtscts=1;
				break;
			case 'h':
				set_hex_mode(1);
				break;
			case 't':
				timestamp = 1;
				break;
			default:
				usage(argv[0]);
				exit(0);
		}
	}
	
	argv += optind;
	argc -= optind;

	int i;
	for(i=0; i<argc; i++) {
		printf("%d %s\n", i, argv[i]);
		int b = get_baudrate(argv[i]);
		if(b > 0) {
			baudrate = b;
		} else {
			strncpy(ttydev, argv[i], sizeof(ttydev));
		}
	}

	if(!strchr(ttydev, '/')) {
		char tmp[32];
		snprintf(tmp, sizeof tmp, "%s", ttydev);
		snprintf(ttydev, sizeof ttydev, "/dev/%s", tmp);
	}

	fd_serial   = serial_open(ttydev, baudrate, rtscts);
	fd_terminal = 0;
	
	msg("Connect to %s at %d bps", ttydev, baudrate);

	mainloop_signal_add(SIGINT, on_sigint, NULL);


	set_noncanonical(fd_serial, NULL);
	set_noncanonical(fd_terminal, &save);

	mainloop_fd_add(fd_serial, FD_READ, on_serial_read, NULL);
	mainloop_fd_add(fd_terminal, FD_READ, on_terminal_read, NULL);
	mainloop_timer_add(0, 100, on_status_timer, NULL);

	mainloop_run();

	msg("Exit");

	tcsetattr (fd_terminal, TCSANOW, &save);
	return(0);
}


static void set_hex_mode(int onoff)
{
	if(onoff) {
		hex_buf[0] = '\0';
		hex_off = 0;
		hex_mode = 1;
	} else {
		printf("\n");
		hex_mode = 0;
	}
	msg("Hex mode %s", onoff ? "enabled" : "disabled");
}


static int get_baudrate(const char *s)
{
	char *p;
	int baudrate = atoi(s);

	p = strchr(s, 'k');
	if(p) {
		baudrate = baudrate * 1000 + atoi(p+1) * 100;
	}
	return baudrate;
}


static void terminal_write(uint8_t c, int local)
{
	char hex[3];

	if(hex_mode) {

		if((hex_off % 16) == 0) {
			if(hex_buf[0]) {
				printf("%s\n", hex_buf);
				fflush(stdout);
			}
			sprintf(hex_buf, "\r%08x                                                    |                |", hex_off);
		}

		int col = hex_off % 16;
		char *p1 = hex_buf + col * 3 + 11 + (col > 7);
		char *p2 = hex_buf + col + 62;
		hex_off ++;

		snprintf(hex, sizeof hex, "%02x", c);
		memcpy(p1, hex, 2);
		*p2 = isprint(c) ? c : '.';

		printf("%s", hex_buf);

	} else {

		if(timestamp) {

			putchar(c);

			if(c == '\n') {
				struct timeval tv;
				gettimeofday(&tv, NULL);
				struct tm *tm = localtime(&tv.tv_sec);
				char tbuf[32] = "";
				strftime(tbuf, sizeof tbuf, "%H:%M:%S", tm);
				printf("\e[1;30m%s.%03d\e[0m ", tbuf, (int)(tv.tv_usec / 1E3));
			}

		} else {
			putchar(c);
		}
	}

	fflush(stdout);

}


static void serial_write(uint8_t c)
{
	write(fd_serial, &c, 1);
	if(echo) terminal_write(c, 1);
}


static int on_serial_read(int fd, void *data)
{
	uint8_t buf[128];
	uint8_t *p = buf;
	int len;

	len = read(fd_serial, buf, sizeof(buf));
	if(len <= 0) {
		if(len == 0) {
			msg("Serial port closed");
		} else {
			msg("Error reading from serial port: %s", strerror(errno));
		}
		mainloop_stop();
	}

	while(len--) terminal_write(*p++, 0);

	return 0;
}


void show_modemstatus(void)
{
	char buf[64];
	char *p = buf;
	int status = serial_get_mctrl(fd_serial);
	p += sprintf(p, "[%s] ", (status & TIOCM_DTR) ? "DTR" : "dtr");
	p += sprintf(p, "[%s] ", (status & TIOCM_DSR) ? "DSR" : "dsr");
	p += sprintf(p, "[%s] ", (status & TIOCM_CD ) ? "DCD" : "dcd");
	p += sprintf(p, "[%s] ", (status & TIOCM_RTS) ? "RTS" : "rts");
	p += sprintf(p, "[%s] ", (status & TIOCM_CTS) ? "CTS" : "cts");
	p += sprintf(p, "[%s] ", (status & TIOCM_RI)  ? "RI" : "ri");
	msg(buf);
}


static int on_status_timer(void *data)
{
	static int pstatus = -1;
	int status;

	status = serial_get_mctrl(fd_serial);

	if(pstatus != -1 && status != pstatus) {
		show_modemstatus();
	}

	pstatus = status;
	return 1;
}


static int on_terminal_read(int fd, void *data)
{
	char c;
	static int in_hex;
	static int escape = 0;
	static int hexval = 0;

	read(fd_terminal, &c, 1);
	
	if(escape) {
		
		c = tolower(c);
	
		if(c == '~') {
			serial_write(c);
			if(echo) {
				terminal_write(c, 1); 
			}
		}
		
		else if(c == '.' || c == '>') {
			putchar('\n');
			mainloop_stop();
			return -1;
		}
		
		else if(c == 'm') {
			show_modemstatus();
		}

		else if(c == 'b') {
			tcsendbreak(fd_serial, 0);
			msg("Break");
			fflush(stdout);
		}
		
		else if(c == 'r') {
			int status = serial_get_mctrl(fd_serial) & TIOCM_RTS;
			serial_set_rts(fd_serial, !status);
		}
		
		else if(c == 'd') {
			int status = serial_get_mctrl(fd_serial) & TIOCM_DTR;
			serial_set_dtr(fd_serial, !status);
		}
		
		else if(c == 'h') {
			set_hex_mode(!hex_mode);
		}
		
		else if(c == 'e') {
			echo = !echo;
			msg("Echo %s", echo ? "enabled" : "disabled");
		}
		
		else if(c == 't') {
			timestamp = !timestamp;
			msg("Timestamps %s", timestamp ? "enabled" : "disabled");
		}
		
		else if(c == 'x') {
			in_hex = 1;
		}
		
		else  {
			msg("~    send tilde");
			msg(".    exit");
			msg("b    send break");
			msg("d    toggle dtr");
			msg("m    show modem status lines");
			msg("h    toggle hex mode");
			msg("e    toggle echo");
			msg("t    toggle timestamp");
			msg("xNN  enter hex character NN");
		}
		
		escape = 0;
		
	} 
	
	else if(in_hex) {
	
		c = tolower(c);
		c = c - '0';
		if(c > 9) c -= 39;
		
		hexval = (hexval << 4) + c;

		if(++in_hex > 2) {
			serial_write(hexval);
			in_hex = 0;
		}
		
	}
		
	
	else {
		if(c == '~') {
			escape = 1;
		} else {
			if(translate_newline) {
				if(c == '\n') c = '\r';
			}
			serial_write(c);
		}
	}
	
	return 0;
}	


void msg(const char *fmt, ...)
{
	char buf[128];
	va_list va;

	if(!have_tty) return;

	va_start(va, fmt);
	vsnprintf(buf, sizeof buf, fmt, va);
	va_end(va);
	
	printf("\r\e[K\e[1;30m> %s\e[0m\n", buf);
}




void usage(char *fname)
{
	printf("usage: %s [-r] [port] [baudrate]\n", fname);
	printf("\n");
	printf("  -b RATE   Set baudrate to RATE\n");
	printf("  -e        Enable local echo\n");
	printf("  -n        translate newline to cr\n");
	printf("  -r	    use RTS/CTS hardware handshaking\n");
	printf("  -h	    HEX mode\n");
}


static int on_sigint(int signo, void *data)
{
	mainloop_stop();
	return 0;
}


/*
 * End
 */
