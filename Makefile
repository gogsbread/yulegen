 #instructions taken from https://github.com/hzeller/rpi-rgb-led-matrix/blob/master/examples-api-use/README.md#integrating-in-your-own-application

CFLAGS=-Wall -O3 -g -Wextra -Wno-unused-parameter
CXXFLAGS=$(CFLAGS) -std=c++17

RGB_LIB_DISTRIBUTION=matrix
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a
RGB_LDFLAGS=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread

# Imagemagic flags, only needed if actually compiled.
MAGICK_CXXFLAGS?=$(shell GraphicsMagick++-config --cppflags --cxxflags)
MAGICK_LDFLAGS?=$(shell GraphicsMagick++-config --ldflags --libs)

LD_FLAGS+=$(RGB_LDFLAGS) $(MAGICK_LDFLAGS) -lstdc++fs

default: yulegen

yulegen:  yulegen.o $(RGB_LIBRARY)
	$(CXX) $(CXXFLAGS) yulegen.o -o $@ $(RGB_LDFLAGS) $(MAGICK_LDFLAGS) -lstdc++fs

yulegen.o: yulegen.cpp
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) $(MAGICK_CXXFLAGS) -c -o $@ $<

$(RGB_LIBRARY):
	$(MAKE) -C $(RGB_LIBDIR)

clean:
	rm -f yulegen.o yulegen