# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2010-2014 Intel Corporation

# binary name
APP = user

# all source are stored in SRCS-y
SRCS-y := *.c

# Add flag 
CFLAGS += -g -Wall -I./

$(APP): $(SRCS-y) Makefile 
	$(CC) $(CFLAGS) $(SRCS-y) -o $@


.PHONY: clean
clean:
	echo "Linux rm"
	rm -f *.o
	rm -f *.exe
	rm -f $(APP)
