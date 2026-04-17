CC      = gcc
LIBS    = -lavformat -lavcodec -lavutil -ljson-c
TARGET  = cutvideo

debug:
	$(CC) -g -O0 main.c -o $(TARGET) $(LIBS)

release:
	$(CC) -O2 -DNDEBUG main.c -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)
