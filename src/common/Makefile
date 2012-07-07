include ../Makefile.inc

SOURCES := $(wildcard *.cpp)

.PHONY: all clean

all: $(COMMON_LIB)

$(COMMON_LIB): $(SOURCES:%.cpp=%.o)
	$(AR) rcs $(COMMON_LIB) $(SOURCES:%.cpp=%.o)

%.o: %.cpp
	$(CXX) -o $@ -c $< -m32 -DBUILD_COMMON -Iinclude -std=gnu++11 $(CXXFLAGS)

clean:
	$(RM) include/*~ *~ *.o $(COMMON_LIB)
