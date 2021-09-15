CC=x86_64-pc-toaru-gcc
LIBDIR=../build/release/
CFLAGS=-I../fitz -s -O2
THIRD_PARTY=$(LIBDIR)libfitz.a  $(LIBDIR)libfreetype.a  $(LIBDIR)libjpeg.a  $(LIBDIR)libopenjpeg.a  $(LIBDIR)libjbig2dec.a  $(LIBDIR)libz.a 
LDLIBS=$(THIRD_PARTY) -ltoaru_decorations -ltoaru_menu -ltoaru_yutani -ltoaru_graphics -ltoaru_hashmap

all: mupdf.tgz

pdfviewer: pdfviewer.c

mupdf.tgz: pdfviewer agpl3.trt mupdf.png
	mkdir -p pkg/usr/share/icons/48
	mkdir -p pkg/usr/share/help
	mkdir -p pkg/usr/bin
	cp pdfviewer pkg/usr/bin/
	cp agpl3.trt pkg/usr/share/help/
	cp mupdf.png pkg/usr/share/icons/48/
	cd pkg && tar -czvf ../mupdf.tgz .
