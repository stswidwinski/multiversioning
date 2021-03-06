# Flags available within the batched system. WARNING: Collection of diagnostics might 
#	directly impact the performance of the system, especially when considering the 
#	timing diagnostics. Make sure that is not a factor when considering using diagnostics
#	or modify the number of measured variables.
#
# 	- SCHEDULER_DIAG -- if defined, allows the system to collect statistics on scheduler
# 											threads and print them.
# 	- SCHEDULER_MAN_DIAG -- if degined, allows the system to collect statistics on
# 													the scheduler manager and print them.
# 	- SCHEDULER_MAN_NO_TIME -- disable timing statistics in diagnostics on scheduler manager.

CFLAGS= $(ADD_CFLAGS) -O2 -g -Wall -Wextra -Werror -std=c++14 -Wno-sign-compare 
CFLAGS+=-DSNAPSHOT_ISOLATION=0 -DSMALL_RECORDS=0 -DREAD_COMMITTED=1
LIBS=-lnuma -lpthread -lrt -lcityhash
TEST_LIBS=-lgtest
CXX=g++-5

LIBPATH=./libs/lib/
INC_DIRS=include libs/include
INCLUDE=$(foreach d, $(INC_DIRS), -I$d)
SRC=src

SOURCES:=$(wildcard $(SRC)/*.cc $(SRC)/*.c)
OBJECTS:=$(patsubst $(SRC)/%.cc,build/%.o,$(SOURCES))

START:=$(wildcard start/*.cc start/*.c)
START_OBJECTS:=$(patsubst start/%.cc,start/%.o,$(START))

BATCHING:=$(wildcard $(SRC)/batch/*.cc)
BATCHING_OBJECTS:=$(patsubst src/batch/%.cc,build/batch/%.o,$(BATCHING))

BATCH_DB:=$(wildcard start_batch/*.cc)
BATCH_DB_OBJECTS:=$(patsubst start_batch/%.cc,start_batch/%.o, $(BATCH_DB))

TIMING:=$(wildcard time_elts/*.cc)
TIMING_OBJECTS:=$(patsubst time_elts/%.cc,time_elts/%.o,$(TIMING))

TEST:=test
TESTSOURCES:=$(wildcard $(TEST)/*.cc)
TESTOBJECTS:=$(patsubst test/%.cc,test/%.o,$(TESTSOURCES))

NON_MAIN_STARTS:=$(filter-out start/main.o,$(START_OBJECTS))

DEPSDIR:=.deps
DEPCFLAGS=-MD -MF $(DEPSDIR)/$*.d -MP

all:CFLAGS+=-DTESTING=0 -DUSE_BACKOFF=1 -fno-omit-frame-pointer
all: build/db

test:CFLAGS+=-DTESTING=1 -DUSE_BACKOFF=1 
test:build/tests

batch: CFLAGS+=-DTESTING=0 -DUSE_BACKOFF=1 -fno-omit-frame-pointer
batch: build/batch_db

time: CFLAGS+=-DTESTING=0 -DUSE_BACKOFF=1
time: build/time_elements

-include $(wildcard $(DEPSDIR)/*.d)

build/%.o: src/%.cc $(DEPSDIR)/stamp GNUmakefile
	@mkdir -p build
	@mkdir -p build/batch
	@echo + cc $<
	@$(CXX) $(CFLAGS) $(DEPCFLAGS) $(INCLUDE) -c -o $@ $<

build/batch/%.o: src/batch/%.cc $(DEPSDIR)/stamp GNUmakefile
	@mkdir -p build/batch
	@echo + cc $<
	@$(CXX) $(CFLAGS) $(DEPCFLAGS) $(INCLUDE) -c -o $@ $<

$(TESTOBJECTS):$(OBJECTS)

test/%.o: test/%.cc $(DEPSDIR)/stamp GNUmakefile
	@echo + cc $<
	@$(CXX) $(CFLAGS) -Wno-missing-field-initializers -Wno-conversion-null $(DEPCFLAGS) -Istart $(INCLUDE) -c -o $@ $<

start/%.o: start/%.cc $(DEPSDIR)/stamp GNUmakefile
	@echo + cc $<
	@$(CXX) $(CFLAGS) $(DEPCFLAGS) $(INCLUDE) -Istart -c -o $@ $<

build/db:$(START_OBJECTS) $(OBJECTS)
	@echo $(INCLUDE)
	@$(CXX) $(CFLAGS) -o $@ $^ -L$(LIBPATH) $(LIBS)

start_batch/%.o: start_batch/%.cc $(DEPSDIR)/stamp GNUmakefile
	@echo + cc $<
	@$(CXX) $(CFLAGS) $(DEPCFLAGS) $(INCLUDE) -Istart_batch -c -o $@ $<

time_elts/%.o: time_elts/%.cc $(DEPSDIR)/stamp GNUmakefile
	@echo + cc $<
	@$(CXX) $(CFLAGS) $(DEPCFLAGS) $(INCLUDE) -Itime_elts -c -o $@ $<

build/time_elements: $(OBJECTS) $(BATCHING_OBJECTS) $(TIMING_OBJECTS)
	@$(CXX) $(CFLAGS) $(INCLUDE) -Itime_elts -o $@ $^ -L$(LIBPATH) $(LIBS)

build/batch_db: $(OBJECTS) $(BATCHING_OBJECTS) $(BATCH_DB_OBJECTS) 
	@$(CXX) $(CFLAGS) $(INCLUDE) -Istart_batch -o $@ $^ -L$(LIBPATH) $(LIBS)

build/tests:$(OBJECTS) $(BATCHING_OBJECTS) $(TESTOBJECTS) $(NON_MAIN_STARTS)
	@$(CXX) $(CFLAGS) $(INCLUDE) -o $@ $^ -L$(LIBPATH) $(LIBS) $(TEST_LIBS)

$(DEPSDIR)/stamp:
	@mkdir -p $(DEPSDIR)
	@mkdir -p $(DEPSDIR)/batch
	@echo $(DEPSDIR)/batch
	@touch $@

.PHONY: clean 

clean:
	rm -rf build $(DEPSDIR) $(TESTOBJECTS) start/*.o test/*.o start_batch/*.o 
