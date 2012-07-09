MANAGER_LIB_NAME = manager
CORE_LIB_NAME = core

COMMON_LIB_NAME = common
COMMON_DIR = common
COMMON_LIB_LDFLAGS =

JSONCPP_LIB_NAME = jsoncpp
JSONCPP_DIR = jsoncpp
JSONCPP_LIB_LDFLAGS =

CXX = g++-4.7
#CXXFLAGS = -Wall -Wextra -pedantic -pipe -fvisibility=hidden -fno-rtti -ffast-math -freciprocal-math -fomit-frame-pointer -mmmx -msse -msse2 -mfpmath=sse -mtune=core2 -D_GLIBCXX_USE_NANOSLEEP -O3 -funroll-loops
CXXFLAGS = -Wall -Wextra -pedantic -pipe -fvisibility=hidden -fno-rtti -ffast-math -freciprocal-math -mmmx -msse -msse2 -mfpmath=sse -mtune=core2 -D_GLIBCXX_USE_NANOSLEEP -ggdb -DDEBUG
AR = ar
RANLIB = ranlib

RM = rm -rf

#############################

ifeq ($(OS),Windows_NT)
    MANAGER_LIB := lib$(MANAGER_LIB_NAME).dll
    CORE_LIB := lib$(CORE_LIB_NAME).dll
else
    MANAGER_LIB := lib$(MANAGER_LIB_NAME).so
    CORE_LIB := lib$(CORE_LIB_NAME).so
endif
COMMON_LIB := lib$(COMMON_LIB_NAME).a
JSONCPP_LIB := lib$(JSONCPP_LIB_NAME).a

ifeq ($(OS),Windows_NT)
    COMMON_LIB_LDFLAGS += -lpsapi -ldbghelp -lntdll
else
    COMMON_LIB_LDFLAGS += -ldl
endif
