include ../Makefile.inc

SOURCES := $(wildcard src/src/lib_json/*.cpp)

.PHONY: all clean

all: $(JSONCPP_LIB)

$(JSONCPP_LIB): $(SOURCES:%.cpp=%.o)
	$(AR) rcs $(JSONCPP_LIB) $(SOURCES:%.cpp=%.o)

%.o: %.cpp include
	$(CXX) -o $@ -c $< -m32 -Iinclude -std=gnu++11 $(CXXFLAGS)

include:
	cp -R src/include .

clean:
	$(RM) include $(SOURCES:%.cpp=%.o) $(JSONCPP_LIB)
