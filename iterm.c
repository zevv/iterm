
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
#include "speed.h"

static int fd_serial;
static int fd_terminal;
static int hex_off = 0;
static int hex_mode = 0;
static int translate_newline = 0;
static char hex_buf[90] = "";
static int timestamp = 0;
static int echo = 0;
static int have_tty;
static int log_enable = 0;
static FILE *fd_log;

static int get_baudrate(const char *s);
static int on_terminal_read(int fd, void *data);
static int on_serial_read(int fd, void *data);
static int on_status_timer(void *data);
static void msg(const char *fmt, ...);
static void usage(char *fname);
static int on_sigint(int signo, void *data);
static void set_hex_mode(int onoff);
static void terminal_write(uint8_t c, int local);
static void log_write(const uint8_t *buf, size_t len);
static void set_log_enable(int onoff, const char *fname);


int main(int argc, char **argv)
{
	struct termios save;
	int o;
	int rtscts = 0;
	int xonxoff = 0;
	int stopbits = 1;
	int set_dtr = 0;
	int set_rts = 0;
	int parity = 0;
	int baudrate = 115200;
	char ttydev[64] = "/dev/ttyUSB0";
	int use_custom_baudrate = 0;
	
	have_tty = isatty(1);
	
	while( (o = getopt(argc, argv, "E2b:cehl:nrtxDR")) != EOF) {
		switch(o) {
			case '2':
				stopbits = 2;
				break;
			case 'e':
				echo = 1;
				break;
			case 'E':
				parity = 1;
				break;
			case 'c':
				use_custom_baudrate = 1;
				break;
			case 'D':
				set_dtr = 1;
				break;
			case 'R':
				set_rts = 1;
				break;
			case 'b':
				baudrate = get_baudrate(optarg);
				break;
			case 'n':
				translate_newline ++;
				break;
			case 'l':
				set_log_enable(1, optarg);
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
			case 'x':
				xonxoff = 1;
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
		int b = get_baudrate(argv[i]);
		if(b > 0) {
			baudrate = b;
		} else {
			snprintf(ttydev, sizeof(ttydev), "%s", argv[i]);
		}
	}

	if(!strchr(ttydev, '/')) {
		char tmp[32];
		snprintf(tmp, sizeof tmp, "%s", ttydev);
		snprintf(ttydev, sizeof ttydev, "/dev/%s", tmp);
	}

	fd_serial   = serial_open(ttydev, baudrate, rtscts, xonxoff, stopbits, parity);
	fd_terminal = 0;

	if(use_custom_baudrate) {
		int s2 = set_speed(fd_serial, baudrate);
		msg("Custom baudrate %d\n", s2);
	}

	int baudrate2 = serial_get_speed(fd_serial);
	
	msg("Connect to %s at %d bps%s%s", ttydev, baudrate2,
			rtscts ? " (RTSCTS)": "",
			xonxoff ? " (XON/XOFF)": ""
			);

	mainloop_signal_add(SIGINT, on_sigint, NULL);

	serial_set_dtr(fd_serial, set_dtr);
	serial_set_rts(fd_serial, set_rts);

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
	if(p) baudrate = baudrate * 1000 + atoi(p+1) * 100;
	
	p = strchr(s, 'm');
	if(p) baudrate = baudrate * 1000000 + atoi(p+1) * 100000;

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
	int r;
	
	r = write(fd_serial, &c, 1);
	if(r < 0) {
		msg("Error writing to serial port: %s", strerror(errno));
		mainloop_stop();
		return;
	}
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

	log_write(buf, len);

	while(len--) terminal_write(*p++, 0);

	return 0;
}


struct mctrl {
	char *name_up;
	char *name_down;
	int mask;
} mctrl_list[] = {
	{ "DTR", "dtr", TIOCM_DTR },
	{ "DSR", "dsr", TIOCM_DSR },
	{ "DCD", "dcd", TIOCM_CD  },
	{ "RTS", "rts", TIOCM_RTS },
	{ "CTS", "cts", TIOCM_CTS },
	{ "RI" , "ri" , TIOCM_RI },
};

void show_modemstatus(void)
{
	char buf[64];
	char *p = buf;
	static int pstatus = -1;
	int i;

	int status = serial_get_mctrl(fd_serial);

	for(i=0; i<sizeof(mctrl_list) / sizeof(mctrl_list[0]); i++) {
		struct mctrl *m = &mctrl_list[i];
		char *updown = " ";

		if(pstatus != -1) {
			if((status & m->mask) && !(pstatus & m->mask)) updown = "↗";
			if(!(status & m->mask) && (pstatus & m->mask)) updown = "↘";
		}
		
		p += sprintf(p, "[%s%s] ", 
				(status & m->mask) ? m->name_up : m->name_down,
				updown);
	}

	msg(buf);
	
	pstatus = status;
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
	uint8_t c;
	static int in_hex;
	static int escape = 0;
	static int hexval = 0;
	int r;

	r = read(fd_terminal, &c, 1);
	if(r < 0) {
		msg("Error reading from terminal");
		mainloop_stop();
		return 0;
	}

	log_write(&c, 1);
	
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
			tcsendbreak(fd_serial, 1);
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
		
		else if(c == 'l') {
			set_log_enable(!log_enable, NULL);
			msg("Logging %s", log_enable ? "enabled" : "disabled");
		}
		
		else if(c == 'x') {
			in_hex = 1;
		}

		else if(isdigit(c)) {

			char fname[64];
			snprintf(fname, sizeof fname, "%s/.iterm-%c", getenv("HOME"), c);
			FILE *f = fopen(fname, "r");
			if(f) {
				char buf[32000];
				int l = fread(buf, 1, sizeof buf, f);
				msg("Writing buffer %c, %d bytes", c, l);
				if(l > 0) {
					int i;
					for(i=0; i<l; i++) serial_write(buf[i]);
					usleep(100);
				}
				fclose(f);
			}
		}
		
		else  {
			msg("~    send tilde");
			msg(".    exit");
			msg("0..9 send contents of ~/iterm-<N> to serial port");
			msg("b    send break");
			msg("d    toggle dtr");
			msg("m    show modem status lines");
			msg("h    toggle hex mode");
			msg("e    toggle echo");
			msg("l    toggle logging");
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


static void log_write(const uint8_t *buf, size_t len)
{
	if(log_enable && fd_log) {
		fwrite(buf, 1, len, fd_log);
		fflush(fd_log);
	}
}


static void set_log_enable(int onoff, const char *fname)
{
	if(onoff) {
		if(fd_log == NULL) {
			if(fname == NULL) fname = "iterm.log";
			fd_log = fopen(fname, "a+");
			if(fd_log) {
				msg("Writing log to %s", fname);
			} else {
				msg("Error opening log: %s", strerror(errno));
			}
		}
		if(fd_log) {
			log_enable = 1;
		}
	} else {
		log_enable = 0;
	}
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
	printf("  -l PATH   Log to given file\n");
	printf("  -r	    use RTS/CTS hardware handshaking\n");
	printf("  -h	    HEX mode\n");
	printf("  -c        Use custom baud rate\n");
	printf("  -x	    Enable XON/XOFF flow control\n");
	printf("  -D	    Set DTR on at startup\n");
	printf("  -R	    Set RTS on at startup\n");
	printf("\n");
	printf("Available baud rates:\n");
	printf("  50 300 1200 2400 4800 9600 19200 38400 57600 115200\n");
	printf("  230400 460800 500000 576000 921600 1000000 1152000 \n");
	printf("  1500000 2000000 2500000 3000000 3500000 4000000\n");
}


static int on_sigint(int signo, void *data)
{
	mainloop_stop();
	return 0;
}


/*
 * End
 */
