include Makefile.inc

DIRS = $(wildcard */)

.PHONY: all clean

all:
	$(foreach DIR, $(DIRS), $(MAKE) -C $(DIR) &&) true

clean:
	$(foreach DIR, $(DIRS), $(MAKE) clean -C $(DIR) &&) true
