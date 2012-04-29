#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/serial.h>

#include "serial.h"

int serial_open(char *dev, int baudrate, int rtscts, int xonxoff)
{
	int fd = 0;
	int br = 0;
	int r;
	struct termios tios;

	fd = open (dev, O_RDWR | O_NOCTTY | O_NDELAY);
	
	if (fd < 0) {
		perror (dev);
		exit (1);
	}

	switch(baudrate) {
		case      50: br =   B50;    break;
		case     300: br =   B300;   break;
		case    1200: br =   B1200;  break;
		case    2400: br =   B2400;  break;
		case    4800: br =   B4800;  break;
		case    9600: br =   B9600;  break;
		case   19200: br =   B19200; break;
		case   38400: br =   B38400; break;
		case   57600: br =   B57600; break;
		case  115200: br =  B115200; break;
		case  230400: br =  B230400; break;
		case  460800: br =  B460800; break;
		case  500000: br =  B500000; break;
		case  576000: br =  B576000; break;
		case  921600: br =  B921600; break;
		case 1000000: br = B1000000; break;
		case 1152000: br = B1152000; break;
		case 1500000: br = B1500000; break;
		case 2000000: br = B2000000; break;
		case 2500000: br = B2500000; break;
		case 3000000: br = B3000000; break;
		case 3500000: br = B3500000; break;
		case 4000000: br = B4000000; break;
		default:      
			      fprintf(stderr, "Illegal baudrate supplied\n");
			      exit(1);
	
	}

	tios.c_cflag = br | CS8 | CLOCAL | CREAD;  
	tios.c_iflag = IGNPAR;
	tios.c_oflag = OPOST;
	if(rtscts) tios.c_cflag |= CRTSCTS; 
	if(xonxoff) tios.c_iflag |= IXON | IXOFF | IXANY;

	tcflush (fd, TCIFLUSH);
	r = tcsetattr (fd, TCSANOW, &tios);
	if(r != 0) printf("tcsetattr : %s\n", strerror(errno));

	return(fd);
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
