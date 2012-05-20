# Bellow assumes you have both $NDKDIR and $HT set properly in environment:
MYCXX ?= g++
LINK ?= g++
CXXFLAGS ?= -g -c -DUSE_GLEW -DLINUX -I$(NDKDIR)/include -I$(HT)/include -msse -DAMD64 -m64 -fPIC -DSIZEOF_VOID_P=8 -DENABLE_THREADS -DUSE_PTHREADS -DSESI_LITTLE_ENDIAN -D_REENTRANT -D_FILE_OFFSET_BITS=64 -c -DGCC4 -DGCC3 -O2 -fno-strict-aliasing
ifeq ($(DEBUG),1)
    CXXFLAGS += -DDEBUG
endif
LINKFLAGS ?= -L$(NDKDIR) -L$(HFS)/dsolib
LIBS ?= -lDDImage -lHoudiniUT 
LINKFLAGS += -shared
all: ratReader.so ratReaderDeep.so
.PRECIOUS : %.os
%.os: %.cpp
	$(MYCXX) $(CXXFLAGS) -o $(@) $<
%.so: %.os
	$(LINK) $(LINKFLAGS) $(LIBS) -o $(@) $<
clean:
	rm -rf *.os ratReader.so ratReaderDeep.so
