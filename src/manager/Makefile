include ../Makefile.inc

ifeq ($(OS),Windows_NT)
    LDFLAGS += -lws2_32
else
    LDFLAGS += -lpthread
endif

COMMON_DIR := ../$(COMMON_DIR)
JSONCPP_DIR := ../$(JSONCPP_DIR)
SOURCES := $(wildcard *.cpp)

.PHONY: all clean $(COMMON_DIR)/$(COMMON_LIB)

all: $(MANAGER_LIB)

$(MANAGER_LIB): $(SOURCES:%.cpp=%.o) $(COMMON_DIR)/$(COMMON_LIB) $(JSONCPP_DIR)/$(JSONCPP_LIB)
	$(CXX) -o $(MANAGER_LIB) $(SOURCES:%.cpp=%.o) -m32 -shared -fPIC -L$(COMMON_DIR) -l$(COMMON_LIB_NAME) $(COMMON_LIB_LDFLAGS) -L$(JSONCPP_DIR) -l$(JSONCPP_LIB_NAME) $(JSONCPP_LIB_LDFLAGS) $(LDFLAGS) -shared-libgcc

%.o: %.cpp $(COMMON_DIR)/include $(JSONCPP_DIR)/include
	$(CXX) -o $@ -c $< -m32 -DBUILD_MANAGER -Iinclude -I$(COMMON_DIR)/include -I$(JSONCPP_DIR)/include -std=gnu++11 $(CXXFLAGS)

$(COMMON_DIR)/include $(COMMON_DIR)/$(COMMON_LIB):
	$(MAKE) -C $(COMMON_DIR)

$(JSONCPP_DIR)/include $(JSONCPP_DIR)/$(JSONCPP_LIB):
	$(MAKE) -C $(JSONCPP_DIR)

clean:
	$(RM) include/*~ *~ *.o $(MANAGER_LIB)
