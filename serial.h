/* serial.c */
int serial_open(char *dev, int baudrate, int rtscts, int xonxoff);
int serial_get_speed(int fd);
int set_noncanonical(int fd, struct termios *save);
int serial_set_dtr(int fd, int state);
int serial_set_rts(int fd, int state);
int serial_get_mctrl(int fd);
