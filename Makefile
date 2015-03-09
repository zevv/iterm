
CC 	= gcc
LD 	= gcc
LDFLAGS +=  
CFLAGS  += -Wall -Werror -O3  -g 

BIN   	= iterm
FILES 	= iterm.o serial.o mainloop.o speed.o

.c.o:
	$(CC) $(CFLAGS) -c $<

$(BIN):	$(FILES)
	$(CC) -o $@ $(FILES) $(LDFLAGS)

clean:	
	rm -f $(FILES) $(BIN) core
