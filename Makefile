CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -pthread -Iinclude
LDLIBS := -pthread -lrt
SRC := src/main.c src/common.c src/scenario.c src/logger.c src/banker.c src/scheduler.c src/patient.c src/records.c src/cache.c src/dashboard.c
OBJ := $(SRC:.c=.o)
DEMOS := demos/fork_sort demos/orphan demos/zombie

.PHONY: all test smoke integration asan clean
all: er_sim $(DEMOS)

er_sim: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

demos/%: demos/%.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

tests/test_banker: tests/test_banker.c src/banker.c src/common.c
	$(CC) $(CFLAGS) -DUNIT_TEST -o $@ $^ $(LDLIBS)
tests/test_sched: tests/test_sched.c src/scheduler.c src/common.c
	$(CC) $(CFLAGS) -DUNIT_TEST -o $@ $^ $(LDLIBS)
tests/test_cache: tests/test_cache.c src/cache.c src/common.c
	$(CC) $(CFLAGS) -DUNIT_TEST -o $@ $^ $(LDLIBS)

test: tests/test_banker tests/test_sched tests/test_cache
	./tests/test_banker
	./tests/test_sched
	./tests/test_cache

smoke: er_sim
	@out=$$(./er_sim --scenario scenarios/classic.csv --no-ui --fast); \
	echo "$$out"; echo "$$out" | grep -q 'Average TAT: 10.50'; \
	echo "$$out" | grep -q 'Average WT: 5.00'

integration: er_sim
	@./er_sim --scenario scenarios/er_rush.csv --no-ui --fast > tests/rush.out
	@grep -q 'EVT=PREEMPT' tests/rush.out
	@grep -q 'EVT=VENT_WAIT' tests/rush.out
	@grep -Eq 'EVT=READ_START.*readers=[2-9]' tests/rush.out
	@grep -q 'EVT=WRITE_START' tests/rush.out
	@grep -q 'EVT=BANKER_DENY.*reason=unsafe' tests/rush.out
	@awk '/EVT=BANKER_DENY.*reason=unsafe/{for(i=1;i<=NF;i++)if($$i~/^PAT=/){split($$i,a,"=");denied[a[2]]=1}} /EVT=BANKER_GRANT/{for(i=1;i<=NF;i++)if($$i~/^PAT=/){split($$i,a,"=");if(denied[a[2]])ok=1}} END{exit !ok}' tests/rush.out
	@awk '/Cache FIFO H\/M=/{split($$3,f,"=");split(f[2],fc,"/");split($$5,l,"=");split(l[2],lc,"/");exit fc[1]==lc[1]&&fc[2]==lc[2]}' tests/rush.out
	@./er_sim --scenario scenarios/er_rush.csv --no-ui --fast --no-sync > tests/rush-nosync.out
	@awk -F'corruption=' '/Records: reads=/{value=$$2} END{exit !(value+0>0)}' tests/rush-nosync.out
	@echo 'PASS rush integration events'
	@rm -f tests/rush.out tests/rush-nosync.out

asan: CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -pthread -Iinclude
asan: clean all

clean:
	rm -f $(OBJ) er_sim $(DEMOS) tests/test_banker tests/test_sched tests/test_cache tests/rush.out hospital.log /dev/shm/er_sim_shm
