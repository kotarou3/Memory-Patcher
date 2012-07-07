include ../Makefile.inc

ifeq ($(OS),Windows_NT)
    LDFLAGS += -lws2_32
else
    LDFLAGS += -lpthread
endif

COMMON_DIR := ../$(COMMON_DIR)
SOURCES := $(wildcard *.cpp)

.PHONY: all clean $(COMMON_DIR)/$(COMMON_LIB)

all: $(CORE_LIB)

$(CORE_LIB): $(SOURCES:%.cpp=%.o) $(COMMON_DIR)/$(COMMON_LIB)
	$(CXX) -o $(CORE_LIB) $(SOURCES:%.cpp=%.o) -m32 -shared -fPIC -L$(COMMON_DIR) -l$(COMMON_LIB_NAME) $(COMMON_LIB_LDFLAGS) $(LDFLAGS) -shared-libgcc

%.o: %.cpp
	$(CXX) -o $@ -c $< -m32 -DBUILD_CORE -Iinclude -I$(COMMON_DIR)/include -std=gnu++11 $(CXXFLAGS)

$(COMMON_DIR)/$(COMMON_LIB):
	$(MAKE) -C $(COMMON_DIR)

clean:
	$(RM) include/*~ *~ *.o $(CORE_LIB)
