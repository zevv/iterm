#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <limits.h>
#include <termios.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/serial.h>

#include "serial.h"


struct speed {
	int speed;
	int bit;
};

struct speed speed_list[] = {
	{      50,      B50 },
	{     300,     B300 },
	{    1200,    B1200 },
	{    2400,    B2400 },
	{    4800,    B4800 },
	{    9600,    B9600 },
	{   19200,   B19200 },
	{   38400,   B38400 },
	{   57600,   B57600 },
	{  115200,  B115200 },
	{  230400,  B230400 },
	{  460800,  B460800 },
	{  500000,  B500000 },
	{  576000,  B576000 },
	{  921600,  B921600 },
	{ 1000000, B1000000 },
	{ 1152000, B1152000 },
	{ 1500000, B1500000 },
	{ 2000000, B2000000 },
	{ 2500000, B2500000 },
	{ 3000000, B3000000 },
	{ 3500000, B3500000 },
	{ 4000000, B4000000 },
};


int serial_open(char *dev, int baudrate, int rtscts, int xonxoff, int stopbits, int parity)
{
	int fd = 0;
	int br = 0;
	int r;
	struct termios tios;
	int i;
	int d_min = INT_MAX;
	struct speed *s;

	for(i=0; i<sizeof(speed_list)/sizeof(speed_list[0]); i++) {
		s = &speed_list[i];

		int d = abs(baudrate - s->speed);
		if(d < d_min) {
			d_min = d;
			br = s->bit;
		}
	}

	if(br == 0) br = B9600;

	fd = open (dev, O_RDWR | O_NOCTTY);
	
	if (fd < 0) {
		perror (dev);
		exit (1);
	}

	tios.c_cflag = br | CS8 | CLOCAL | CREAD;  
	if(stopbits == 2) tios.c_cflag |= CSTOPB;
	if(parity) tios.c_cflag |= PARENB;
	tios.c_iflag = IGNPAR;
	tios.c_oflag = OPOST;
	if(rtscts) tios.c_cflag |= CRTSCTS; 
	if(xonxoff) tios.c_iflag |= IXON | IXOFF | IXANY;

	tcflush (fd, TCIFLUSH);
	r = tcsetattr (fd, TCSANOW, &tios);
	if(r != 0) printf("tcsetattr : %s\n", strerror(errno));

	return fd;
}


int serial_get_speed(int fd)
{
	struct termios tios;
	tcgetattr (fd, &tios);

	int br = cfgetispeed(&tios);
	int i;
	for(i=0; i<sizeof(speed_list)/sizeof(speed_list[0]); i++) {
		if(speed_list[i].bit == br) {
			return speed_list[i].speed;
		}
	}

	return(0);
}


int set_noncanonical(int fd, struct termios *save)
{
	int r;
	struct termios tios;
	
	if(save) tcgetattr(fd, save);
	tcgetattr(fd, &tios);

	tios.c_lflag     = 0;
	tios.c_cc[VTIME] = 0;
	tios.c_cc[VMIN]  = 1;

	tcflush (fd, TCIFLUSH);
	r = tcsetattr (fd, TCSANOW, &tios);
	if(r != 0) printf("tcsetattr : %s\n", strerror(errno));

	return(0);
}


int serial_set_rts(int fd, int state)
{
	int status;
	
	ioctl(fd, TIOCMGET, &status);
	if(state == 1) {
		status |= TIOCM_RTS;
	} else {
		status &= ~TIOCM_RTS;
	}
	ioctl(fd, TIOCMSET, &status);
	return(0);
}

int serial_set_dtr(int fd, int state)
{
	int status;
	
	ioctl(fd, TIOCMGET, &status);
	if(state == 1) {
		status |= TIOCM_DTR;
	} else {
		status &= ~TIOCM_DTR;
	}
	ioctl(fd, TIOCMSET, &status);
	return(0);
}


int serial_get_mctrl(int fd)
{
	int status;
	ioctl(fd, TIOCMGET, &status);
	return status;
}


// end
