#
#   Copyright (C) 2021 Carnegie Mellon University
#
#   This code is distributed "AS IS" without warranty of any kind under
#   the terms of the GNU General Public Licence Version 2.
#

footlog:  footlog.o usbstuff.o evstuff.o
	cc -g -o footlog footlog.o usbstuff.o evstuff.o

footlog.o: footlog.c footlog.h
	cc -c -g footlog.c

usbstuff.o: usbstuff.c footlog.h
	cc -c -g usbstuff.c

evstuff.o: evstuff.c footlog.h
	cc -c -g evstuff.c

clean: 
	rm -f footlog footlog.o usbstuff.o evstuff.o

