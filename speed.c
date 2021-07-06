#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <linux/termios.h>

int ioctl(int d, int request, ...);

int set_speed(int fd, int baud)
{
	struct termios2 t;

	if (ioctl(fd, TCGETS2, &t))
	{
		perror("TCGETS2");
		return 3;
	}

	t.c_cflag &= ~CBAUD;
	t.c_cflag |= BOTHER;
	t.c_ispeed = baud;
	t.c_ospeed = baud;

	if (ioctl(fd, TCSETS2, &t))
	{
		perror("TCSETS2");
		return 4;
	}

	if (ioctl(fd, TCGETS2, &t))
	{
		perror("TCGETS2");
		return 5;
	}

	return t.c_ospeed;
}

/*
 * End
 */
