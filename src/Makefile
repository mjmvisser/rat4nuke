# Compiler Info ('g++-4.1 --version')
# g++-4.1 (GCC) 4.1.2 20080704 (Red Hat 4.1.2-46)
# Copyright (C) 2006 Free Software Foundation, Inc.
# This is free software; see the source for copying conditions.  There is NO
# warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# End Compiler Info Output
# -Wall -W -Wno-parentheses -Wno-sign-compare -Wno-reorder -Wno-uninitialized -Wunused -Wno-unused-parameter -O2 -fno-strict-aliasing
# -Wno-deprecated -Wall -W -Wno-parentheses -Wno-sign-compare -Wno-reorder -Wno-uninitialized -Wunused -Wno-unused-parameter
#NDKDIR ?= /opt/nuke
# Bellow assumes you have both $NDKDIR and $HT set properly in environment:
MYCXX ?= g++
LINK ?= g++
CXXFLAGS ?= -g -c -DUSE_GLEW -DLINUX -I$(NDKDIR)/include -I$(HT)/include -msse -DAMD64 -m64 -fPIC -DSIZEOF_VOID_P=8 -DENABLE_THREADS -DUSE_PTHREADS -DSESI_LITTLE_ENDIAN -D_REENTRANT -D_FILE_OFFSET_BITS=64 -c -DGCC4 -DGCC3 -O2 -fno-strict-aliasing
ifeq ($(DEBUG),1)
    CXXFLAGS += -DDEBUG
endif
LINKFLAGS ?= -L$(NDKDIR) -L$(HFS)/dsolib
LIBS ?= -lDDImage -lHoudiniUI -lHoudiniOPZ -lHoudiniOP3 -lHoudiniOP2 -lHoudiniOP1 -lHoudiniUT -lHoudiniPRM -lpthread
LINKFLAGS += -shared
all: ratReader.so
.PRECIOUS : %.os
%.os: %.cpp
	$(MYCXX) $(CXXFLAGS) -o $(@) $<
%.so: %.os
	$(LINK) $(LINKFLAGS) $(LIBS) -o $(@) $<
clean:
	rm -rf *.os ratReader.so
