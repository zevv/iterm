
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

static int get_baudrate(const char *s);
static int on_terminal_read(int fd, void *data);
static int on_serial_read(int fd, void *data);
static int on_status_timer(void *data);
void msg(const char *fmt, ...);
void usage(char *fname);
int write_all(int fd, const void *buf, size_t count);
static int on_sigint(int signo, void *data);
static void set_hex_mode(int onoff);

int main(int argc, char **argv)
{
	struct termios save;
	int o;
	int rtscts = 0;
	int baudrate = 115200;
	char ttydev[32] = "/dev/ttyS0";
	
	while( (o = getopt(argc, argv, "b:hnrt")) != EOF) {
		switch(o) {
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

	if(argc >= 1) strncpy(ttydev, argv[0], sizeof(ttydev));
	if(argc >= 2) baudrate = get_baudrate(argv[1]);

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


static int on_serial_read(int fd, void *data)
{
	uint8_t buf[128];
	uint8_t *p = buf;
	char hex[3];
	int len;
	int i;

	len = read(fd_serial, buf, sizeof(buf));
	if(len <= 0) {
		if(len == 0) {
			msg("Serial port closed");
		} else {
			msg("Error reading from serial port: %s", strerror(errno));
		}
		mainloop_stop();
	}

	if(hex_mode) {

		for(i=0; i<len; i++) {

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

			snprintf(hex, sizeof hex, "%02x", *p);
			memcpy(p1, hex, 2);
			*p2 = isprint(*p) ? *p : '.';
			p++;
		}

		printf("%s", hex_buf);
		fflush(stdout);

	} else {

		if(timestamp) {

			for(i=0; i<len; i++) {
				putchar(*p);

				if(*p == '\n') {
					struct timeval tv;
					gettimeofday(&tv, NULL);
					struct tm *tm = localtime(&tv.tv_sec);
					char tbuf[32] = "";
					strftime(tbuf, sizeof tbuf, "%H:%M:%S", tm);
					printf("\e[1;30m%s.%03d\e[0m ", tbuf, (int)(tv.tv_usec / 1E3));
				}
				p++;
			};
			fflush(stdout);

		} else {
			fwrite(buf, 1, len, stdout);
			fflush(stdout);
		}
	}

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
			write_all(fd_serial, &c, 1);
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
			write_all(fd_serial, &hexval, 1);
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
			write_all(fd_serial, &c, 1);
		}
	}
	
	return 0;
}	


void msg(const char *fmt, ...)
{
	char buf[128];
	va_list va;

	if(!isatty(1)) return;

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
	printf("  -n        translate newline to cr\n");
	printf("  -r	    use RTS/CTS hardware handshaking\n");
	printf("  -h	    HEX mode\n");
}


int write_all(int fd, const void *buf, size_t count)
{
	fd_set fds;
	size_t written = 0;
	int r;

	while(written < count) {

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		r = select(fd+1, NULL, &fds, NULL, NULL);
		if(r < 0) return r;

		r = write(fd, buf+written, count-written);
		if(r < 0) return r;

		written += r;
	}

	return written;
}


static int on_sigint(int signo, void *data)
{
	mainloop_stop();
	return 0;
}


/*
 * End
 */
