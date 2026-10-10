CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -pthread -Iinclude
LDLIBS := -pthread -lrt
SRC := src/main.c src/common.c src/scenario.c src/logger.c src/banker.c src/scheduler.c src/patient.c src/records.c src/cache.c src/dashboard.c
OBJ := $(SRC:.c=.o)
DEMOS := demos/fork_sort demos/orphan demos/zombie

.PHONY: all test smoke asan clean
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

asan: CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -pthread -Iinclude
asan: clean all

clean:
	rm -f $(OBJ) er_sim $(DEMOS) tests/test_banker tests/test_sched tests/test_cache hospital.log /dev/shm/er_sim_shm
