TARGET	= teensy-loader
DESTDIR = /usr/local/bin

CC 	= gcc
CSRC	= teensy-loader.c
CFLAGS 	= -O2 -Wall


teensy-loader: teensy-loader.c
	$(CC) $(CFLAGS) -o $(TARGET) -s -DUSE_LIBUSB $(CSRC) -lusb $(LDFLAGS)

install: teensy-loader
	sudo mv $(TARGET) $(DESTDIR)

uninstall:
	sudo rm -f $(DESTDIR)/$(TARGET)

clean:
	rm -f $(TARGET)
