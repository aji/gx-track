BIN = gx-track
OBJ = gx-track.o play.o \
	gens-stubs.o \
	gens-sound/ym2612.o

CC = gcc
LD = gcc

CFLAGS = -g \
	$(shell pkg-config --cflags sdl) \
	$(shell pkg-config --cflags gl)

LIBS = -lSDL_image -lm \
	$(shell pkg-config --libs sdl) \
	$(shell pkg-config --libs gl)

$(BIN): $(OBJ)
	$(LD) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $^
%.o: %.s
	$(CC) $(CFLAGS) -c -o $@ $^

clean:
	rm -f $(BIN) $(OBJ)
