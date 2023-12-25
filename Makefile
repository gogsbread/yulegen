 #instructions taken from https://github.com/hzeller/rpi-rgb-led-matrix/blob/master/examples-api-use/README.md#integrating-in-your-own-application

CFLAGS=-Wall -O3 -g -Wextra -Wno-unused-parameter
CXXFLAGS=$(CFLAGS) -std=c++17

BINARY=yulegen
INSTALL_PATH=/usr/local/bin
DATA_PATH=/etc/$(BINARY)
SYSTEMD_SERVICE_PATH=/etc/systemd/system

RGB_LIB_DISTRIBUTION=matrix
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a
RGB_LDFLAGS=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread

# Imagemagic flags, only needed if actually compiled.
MAGICK_CXXFLAGS?=$(shell GraphicsMagick++-config --cppflags --cxxflags)
MAGICK_LDFLAGS?=$(shell GraphicsMagick++-config --ldflags --libs)

# httplib flags
HTTPLIB_LDFLAG=-lssl -lcrypto

LD_FLAGS+=$(RGB_LDFLAGS) $(MAGICK_LDFLAGS) $(HTTPLIB_LDFLAG) -lstdc++fs

default: yulegen

$(BINARY): yulegen.o $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) yulegen.o -o $@ $(LD_FLAGS)

yulegen.o: yulegen.cpp
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) $(MAGICK_CXXFLAGS) -c -o $@ $<

$(RGB_LIBRARY):
	$(MAKE) -C $(RGB_LIBDIR)

clean:
	rm -f yulegen.o yulegen

install:
	install -m 755 $(BINARY) $(INSTALL_PATH)
	mkdir -p $(DATA_PATH)/bootstrap_imgs
	cp -r bootstrap_imgs/* $(DATA_PATH)/bootstrap_imgs
	install -m 644 $(BINARY).env $(DATA_PATH)/$(BINARY).env
	install -m 644 $(BINARY)-start.service $(SYSTEMD_SERVICE_PATH)/$(BINARY)-start.service
	install -m 644 $(BINARY)-start.timer $(SYSTEMD_SERVICE_PATH)/$(BINARY)-start.timer
	install -m 644 $(BINARY)-stop.service $(SYSTEMD_SERVICE_PATH)/$(BINARY)-stop.service
	install -m 644 $(BINARY)-stop.timer $(SYSTEMD_SERVICE_PATH)/$(BINARY)-stop.timer
	systemctl daemon-reload
	systemctl enable $(BINARY)-start.timer
	systemctl enable $(BINARY)-stop.timer
	systemctl start $(BINARY)-start.timer
	systemctl start $(BINARY)-stop.timer

uninstall:
	systemctl stop $(BINARY)-start.timer
	systemctl stop $(BINARY)-stop.timer
	systemctl disable $(BINARY)-start.timer
	systemctl disable $(BINARY)-stop.timer
	rm -f $(SYSTEMD_SERVICE_PATH)/$(BINARY)-start.service
	rm -f $(SYSTEMD_SERVICE_PATH)/$(BINARY)-start.timer
	rm -f $(SYSTEMD_SERVICE_PATH)/$(BINARY)-stop.service
	rm -f $(SYSTEMD_SERVICE_PATH)/$(BINARY)-stop.timer
	systemctl daemon-reload
	rm -f $(DATA_PATH)/$(BINARY).env
	rm -rf $(DATA_PATH)/bootstrap_imgs
	rm -f $(INSTALL_PATH)/$(BINARY)
