# we assume Nuke and Houdini are in your PATH 

# set these appropriately for your version of Nuke
CXX?=g++412
LINK=$(CXX)
NUKE_VERSION?=7.0

HFS=$(abspath $(dir $(shell which hcustom)/..))
HOUDINI_CXXFLAGS=$(shell hcustom -c) -DMAKING_DSO
HOUDINI_LDFLAGS=$(shell hcustom -m) -L$(HFS)/dsolib -Wl,-rpath=$(HFS)/dsolib

NUKE_EXEC=$(shell which Nuke$(firstword $(subst v, ,$(NUKE_VERSION))))
NDKDIR=$(abspath $(dir $(NUKE_EXEC)))
NUKE_CXXFLAGS=-DUSE_GLEW -I$(NDKDIR)/include -fPIC -msse
NUKE_LDFLAGS=-L$(NDKDIR) -lDDImage

CXXFLAGS=-pipe $(NUKE_CXXFLAGS) $(HOUDINI_CXXFLAGS)
LDFLAGS=-shared $(NUKE_LDFLAGS) $(HOUDINI_LDFLAGS)

ifeq ($(DEBUG),1)
    CXXFLAGS+=-DDEBUG -g
    LDFLAGS+=-g
else
	CXXFLAGS+=-DNDEBUG
endif

all: ratReader.so ratReaderDeep.so
.PRECIOUS : %.o
%.so: %.o
	$(LINK) $(LDFLAGS) -o $(@) $<

clean:
	rm -rf *.o ratReader.so ratReaderDeep.so

install:
	mv *.so ~/.nuke
