SRC=main.c audio.c charset.c ui.c blip_buf.c ym3438.c
FILES=$(SRC) blip_buf.h ym3438.h
LIBS=-lSDL2
PROGNAME=pseym
CFLAGS=-std=c99 -Wall
CC=gcc
RM=rm

ifeq ($(OS),Windows_NT)
	LIBS=-lmingw32 -lSDL2main -lSDL2
	MINGW=C:/MinGW
	CFLAGS+=-Wl,-subsystem,windows -I$(MINGW)/include -L$(MINGW)/lib
	PROGNAME=pseym.exe
	RM=del
endif

pseym: $(FILES)
	$(CC) -o $(PROGNAME) $(CFLAGS) $(SRC) $(LIBS)

.PHONY: clean
clean:
	$(RM) $(PROGNAME)

