MAIN = main
TEST = test
CXX = g++-4.7
RM = rm -rf
INSTALL = install
CXXFLAGS =
LDFLAGS =

LIBRARYCXXFLAGS = -I../../../src/common/include -I../../../src/manager/include -m32 -std=gnu++11
LIBRARYLDFLAGS = -L../../../src/manager -lmanager -shared-libgcc -m32

.PHONY: all install clean

all: $(MAIN) $(TEST)

$(MAIN): main.o
	$(CXX) -o $@ $< $(LIBRARYLDFLAGS) $(LDFLAGS)

$(TEST): test.o
	$(CXX) -o $@ $< $(LIBRARYLDFLAGS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) -o $@ -c $< $(LIBRARYCXXFLAGS) $(CXXFLAGS)

install: $(MAIN) $(TEST)
	$(INSTALL) $(MAIN) $(TEST) ..
	$(INSTALL) ../../../src/manager/libmanager.* ../../../src/core/libcore.* ..
	mkdir -p ../include
	$(INSTALL) ../../../src/common/include/* ../../../src/core/include/* ../include
	mkdir -p ../plugins/manager
	mkdir -p ../plugins/core
	mkdir -p ../plugins/include

clean:
	$(RM) *~ *.o $(MAIN) $(TEST)
