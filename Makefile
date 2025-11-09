EXECBIN  = httpproxy
SOURCES  = $(wildcard *.c)
HEADERS  = $(wildcard *.h)
OBJECTS  = $(SOURCES:%.c=build/%.o)
FORMATS  = $(SOURCES:%.c=.format/%.c.fmt) $(HEADERS:%.h=.format/%.h.fmt)
AUX      = test_files test_scripts test_repo.sh .format workloads

CC       = clang
FORMAT   = clang-format
CFLAGS   = -Wall -Wpedantic -Werror -Wextra -O3 -g #-DDEBUG

.PHONY: all clean format

all: $(EXECBIN)

$(EXECBIN): $(OBJECTS) asgn5_helper_funcs.a
	$(CC) -o $@ $^ -pthread

build/%.o : %.c $(HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(EXECBIN) $(OBJECTS)



nuke: clean
	rm -rf .format
purge: clean
	rm -rf $(AUX)

format: $(FORMATS)

.format/%.c.fmt: %.c
	mkdir -p .format
	$(FORMAT) -i $<
	touch $@

.format/%.h.fmt: %.h
	mkdir -p .format
	$(FORMAT) -i $<
	touch $@
