CC      = gcc

PACKAGES = json-c, libavcodec, libavformat, libavutil

CFLAGS += $(shell pkg-config --cflags $(PACKAGES))
LDLIBS += $(shell pkg-config --libs $(PACKAGES))

CFLAGS += -Wall -Wextra

TARGET  = cutvideo

debug:
	$(CC) -g -O0 main.c -o $(TARGET) $(CFLAGS) $(LDLIBS)

release:
	$(CC) -O2 -DNDEBUG main.c -o $(TARGET) $(CFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)
