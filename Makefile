
CC 	= gcc
LD 	= gcc
LDFLAGS += 
CFLAGS  += -Wall -Werror -O3 

BIN   	= iterm
FILES 	= iterm.o serial.o

.c.o:
	$(CC) $(CFLAGS) -c $<

$(BIN):	$(FILES)
	$(LD) -o $@ $(FILES) $(LDFLAGS)

clean:	
	rm -f $(FILES) $(BIN) core
