all: mngplay

mngplay:
	gcc mngplay.c -lmng -lSDL -o mngplay

install:
	cp mngplay /usr/local/bin

clean:
	rm -f *.o mngplay

