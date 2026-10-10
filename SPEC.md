# SPEC: Hospital ER Simulator (Operating Systems Lab mini project)

Course: Operating Systems Laboratory (AID20090), T.Y. B.Tech CSE (AI & DS), Sem V
Platform: Linux (WSL2 is fine). Language: C11. Compiler: gcc.

This file is the full instruction set for the coding agent. Read all of it before writing code. Follow the build order in section 12 and do not skip the tests.

---

## 1. Goal

Build one menu-driven, multi-process C program that simulates a hospital emergency room and combines every topic from the lab manual:

| Lab assignment | Topic | Where it appears in the project |
|---|---|---|
| 1 | Linux commands, shell scripting, arithmetic | `demo.sh` (menu, build, run, stats with `$(( ))` and `awk`) |
| 2 | fork(), orphan, zombie, wait() | Each patient is a forked process. Separate zombie and orphan demos. Fork-sort demo. |
| 3 | FCFS and SRTF scheduling | Treatment slot scheduler, switchable with `--policy fcfs\|srtf` |
| 4 | IPC with pipes | Every process sends log events through one pipe to the logger process |
| 5 | Banker's algorithm | Blood bags, ICU beds and surgery rooms are granted only if the state stays safe |
| 6 | Semaphores, readers-writers | Ventilators (counting semaphore). Records DB (readers-writers). |
| 7 | FIFO and LRU page replacement | "Recent patients" cache, FIFO and LRU run side by side on the same lookups |

Rules that apply everywhere:
- Use **processes**, not threads. The manual teaches processes.
- No external libraries. Only libc, POSIX shared memory (`shm_open`, `mmap`) and POSIX semaphores (`sem_init` with `pshared = 1`).
- Every `.c` file starts with a comment that names the lab assignment it covers and explains the algorithm in 3 to 6 lines. The students must be able to explain every line in the viva.
- Runs must be repeatable. Use a fixed seed (`--seed`, default 42) and fixed scenario files.

---

## 2. Repository layout

```
er-sim/
  Makefile
  demo.sh
  README.md
  SPEC.md
  include/
    common.h          shared-memory struct, enums, constants, event format
  src/
    main.c            admission desk: arg parsing, setup, forks patients, reaps them, cleanup
    scheduler.c       tick loop, FCFS and SRTF selection, statistics
    patient.c         patient process life cycle
    banker.c          Banker's algorithm (pure functions + shared-memory wrapper)
    records.c         readers-writers records DB, doctor and nurse processes
    cache.c           FIFO and LRU caches (pure functions + wrapper)
    logger.c          reads the pipe, writes hospital.log
    dashboard.c       ANSI terminal dashboard
    scenario.c        scenario file loader and validator
  demos/
    fork_sort.c       Assignment 2a: parent sorts ascending, child sorts descending
    orphan.c          Assignment 2b
    zombie.c          Assignment 2c
  scenarios/
    classic.csv       the 4-process example from the manual
    er_rush.csv       10 patients, resources and ventilators under pressure
    er_light.csv      5 patients, relaxed
  tests/
    test_banker.c
    test_sched.c
    test_cache.c
  docs/
    report.md         lab report in the manual's format
    diagrams/         block diagram, flowcharts
```

Design principle: **keep the algorithms pure.** `banker_is_safe()`, `srtf_pick()`, `fcfs_pick()`, `fifo_access()`, `lru_access()` take plain arrays and return results. They never touch shared memory or semaphores. The IPC code is a thin wrapper around them. This makes unit tests easy and keeps the code readable.

---

## 3. Process model

Runtime process tree:

```
er_sim (main: admission desk + reaper)
 |-- logger          reads pipe, writes hospital.log
 |-- scheduler       owns the clock, picks who gets treated each tick
 |-- dashboard       read-only, redraws terminal (skipped with --no-ui)
 |-- doctor_1..R     reader processes
 |-- nurse_1..W      writer processes
 |-- patient_N       one forked process per patient, created at its arrival tick
```

Defaults: R = 3 doctors (readers), W = 2 nurses (writers). Configurable in the scenario file.

Main loop of `er_sim`:
1. Parse args, load and validate the scenario, set up shared memory, semaphores and the pipe.
2. Fork logger, scheduler, dashboard, doctors, nurses.
3. Loop until `shm->sim_done`: for every patient whose `arrival <= shm->tick` and not yet forked, `fork()` a patient process. Call `waitpid(-1, WNOHANG)` to reap finished patients (log `REAPED`). Sleep 20 ms.
4. When done: wait for all children except the logger, close the main write end of the pipe, wait for the logger (it exits on EOF), print the summary, `shm_unlink`, exit.

Signal handling: SIGINT and SIGTERM in main set a flag. Main then kills all children, waits for them, runs the same cleanup. Always `shm_unlink` at startup too, to remove stale memory from a previous crash.

---

## 4. Shared memory

One region, name `/er_sim_shm`, created by main with `shm_open` + `ftruncate` + `mmap(MAP_SHARED)`. Every child inherits the mapping through `fork()`, and the dashboard can also attach by name.

Contents of `struct Shared` (in `common.h`):
- Config: policy, tick_ms, resource totals, ventilator count, counts of doctors and nurses, cache size.
- Clock: `volatile int tick`, `volatile int sim_done`.
- Patient table: `Patient p[MAX_PATIENTS]` with `MAX_PATIENTS = 16`. Each entry has: id, pid, state, arrival, burst, remaining, severity, needs_vent, has_vent, max[3], alloc[3], req1[3], req2[3], req2_at (remaining value that triggers the second request), start_tick, completion_tick, and two semaphores `sem_t go, done`.
- Banker's: `sem_t banker_mutex`.
- Ventilators: `sem_t vent` (counting semaphore, initial value = number of ventilators).
- Records DB: `Record rec[MAX_PATIENTS]`, `sem_t rw_mutex, rw_wrt`, `int read_count`, counters for reads, writes and detected corruption.
- Cache: `sem_t cache_mutex`, FIFO and LRU state, hit and miss counters for both.
- Dashboard feed: last 10 log lines (ring buffer, written by logger), current running patient id, last Gantt entries.

All `sem_t` live inside the struct and are initialised once in main with `sem_init(&s, 1, value)`. The `1` means "shared between processes". This is why shared memory is required: a semaphore in ordinary heap memory is copied by `fork()` and would no longer be shared.

Patient states: `EMPTY, ARRIVED, WAIT_VENT, WAIT_RES, READY, RUNNING, DONE`.

---

## 5. Module requirements

### 5.1 Scheduler (Assignment 3)

The scheduler process is a tick loop. One tick is one unit of treatment time. One treatment slot exists (like one CPU).

Each tick:
1. Build the list of patients in state `READY` (arrived, holds a ventilator if needed, holds its current Banker's grant).
2. Pick one:
   - `srtf`: smallest `remaining`. Ties: higher severity first, then lower id. Preemptive: re-evaluated every tick.
   - `fcfs`: if a patient is currently running, keep it until done (non-preemptive). Otherwise pick the earliest arrival (ties: lower id).
3. If a different patient than last tick was running and the previous one is not finished, log `PREEMPT`.
4. `sem_post(&p->go)`, then `sem_wait(&p->done)`. The patient process does exactly one unit of work between those two calls.
5. Record the Gantt entry. Increment `shm->tick`. If no patient was READY, log `IDLE`.
6. Stop when every patient in the scenario is `DONE`: set `sim_done`.

Statistics at the end, using the formulas from the manual:
- Completion Time (CT) = tick at which the patient finished.
- Turnaround Time (TAT) = CT - arrival.
- Waiting Time (WT) = TAT - burst.
- Print a table plus average TAT and average WT.

Stuck detector: if no patient is READY, no arrival is pending, and some patient is in `WAIT_RES` for 50 consecutive ticks, log `ERROR stuck` and stop. With correct Banker's logic this must never happen. The detector exists to make bugs obvious.

Known limitation to mention in the report: SRTF can starve long cases when short ones keep arriving. That is a property of the algorithm, not a bug.

### 5.2 Patient process (Assignment 2, 3, 5, 6)

Created by `fork()` in main at the arrival tick. Life cycle:

```
state = ARRIVED, log ARRIVE
if needs_vent:
    state = WAIT_VENT
    sem_wait(&shm->vent)                   // blocks if all ventilators are in use
    has_vent = 1, log VENT_ACQ
state = WAIT_RES
loop: banker_request(slot, req1) until granted   // poll every 50 ms, log DENY once, GRANT once
state = READY
while remaining > 0:
    sem_wait(&go)                          // scheduler picks us
    state = RUNNING
    remaining--, log RUN
    if remaining == 0:
        banker_release(slot); if has_vent: sem_post(&vent), log VENT_REL
        state = DONE, completion_tick = tick
        sem_post(&done); exit(0)
    if remaining == req2_at and req2 is non-zero:
        state = WAIT_RES                   // set BEFORE posting done
        sem_post(&done)
        loop: banker_request(slot, req2) until granted
        state = READY
    else:
        state = READY
        sem_post(&done)
```

Important: set the new state **before** `sem_post(&done)`, and never block on Banker's retry while the scheduler is waiting on `done`. Otherwise the scheduler hangs.

### 5.3 Ventilators and the deadlock-avoidance order (Assignment 6)

Ventilators are a counting semaphore initialised to the number of ventilators (default 5). A patient with `needs_vent = 1` takes one and holds it until discharge.

**Lock ordering rule: acquire the ventilator first, then request Banker's resources.** A patient that is waiting for a ventilator holds nothing, so no circular wait can form between the semaphore and the Banker's resources. If the order were reversed (Banker's resources first, ventilator second), patient A could hold blood and wait for a ventilator while patient B holds a ventilator and waits for blood. That is a deadlock the Banker's algorithm cannot see, because ventilators are not part of its matrices. Explain this rule in the report and viva; it is a strong point.

### 5.4 Banker's algorithm (Assignment 5)

Resource types (m = 3): blood bags, ICU beds, surgery rooms. Totals come from the scenario file.

Functions (in `banker.c`):
- `bool banker_is_safe(int n, int m, const int avail[], const int alloc[][M], const int need[][M], int safe_seq[])` implements the Safety Algorithm from the manual (Work, Finish, find i with Need_i <= Work, release, repeat).
- `int banker_try_request(...)` implements the Resource-Request Algorithm in this order: check `Request <= Need` (else error), check `Request <= Available` (else wait), tentatively allocate, run safety, commit if safe or roll back if unsafe. Returns `GRANTED`, `WAIT_UNAVAILABLE`, `WAIT_UNSAFE` or `ERROR_EXCEEDS_MAX`.
- Wrapper `banker_request(slot, req)` locks `banker_mutex`, builds the n x m arrays from the patient table (only patients in states other than `EMPTY` and `DONE`), calls the pure function, copies back, unlocks, logs the result. When granted it stores the safe sequence in shared memory for the dashboard.
- `banker_release(slot)` returns all of the patient's allocation to Available.

Scenario validation: reject any patient whose `max` exceeds the total of any resource (the initial state would be unsafe).

### 5.5 Readers-writers records DB (Assignment 6)

Records DB in shared memory: one record per patient id, each with fields `a`, `b`, `sum` and a `version`. Invariant: `a + b == sum`.

First readers-writers solution, exactly as in the manual: semaphores `rw_mutex = 1` (protects `read_count`), `rw_wrt = 1`, integer `read_count`.

- Doctor (reader) loop: pick a patient id, run the reader protocol, read `a`, `b`, `sum`, check the invariant (count `corrupt` if broken), log `READ_START` and `READ_END` with the current reader count, sleep a random 100 to 400 ms, repeat. Each read also performs one cache lookup (section 5.6).
- Nurse (writer) loop: pick a patient id, `sem_wait(&rw_wrt)`, write `a`, `usleep` a short time, write `b` and update `sum`, `sem_post(&rw_wrt)`, log `WRITE_START` and `WRITE_END`, sleep a random 300 to 800 ms, repeat.
- Flag `--no-sync` skips the semaphores. The short `usleep` between writing `a` and `b` makes the corruption counter go up. Showing the counter at 0 with sync and above 0 without sync is the best demonstration of why synchronisation is needed. Put it in the demo menu.
- Doctors and nurses stop when `sim_done` is set.

### 5.6 FIFO vs LRU cache (Assignment 7)

A "recent patients" cache of N frames (default 4) holds patient ids. Every doctor read calls `cache_access(patient_id)` under `cache_mutex`. Both policies process the **same lookup stream**, each with its own frames and counters:
- `fifo_access(state, page)`: hit if present, else load into a free frame, else evict the oldest loaded.
- `lru_access(state, page)`: hit if present (update recency), else load into a free frame, else evict the least recently used.
- Both log `CACHE hit/miss` and the frame contents.
- The dashboard shows hits, misses and hit rate for each side by side.

Also provide `--demo cache`: replay the manual's reference string `1,2,3,4,2,1,5,6,2,1,2,3,7,6,3,2,1,2,3,6` with 3 frames and print the per-step table (page, hit or fault, frames) for FIFO, LRU and Optimal. Expected totals, which must appear in `test_cache.c`:

| Algorithm | Faults | Hits |
|---|---|---|
| FIFO | 16 | 4 |
| LRU | 15 | 5 |
| Optimal | 11 | 9 |

### 5.7 Logger and pipe (Assignment 4)

- Main creates one pipe with `pipe(fd)` **before the first fork**.
- Logger: closes `fd[1]`, reads `fd[0]` in a loop, appends each line to `hospital.log`, pushes it to the dashboard ring buffer in shared memory, and exits when `read()` returns 0 (EOF).
- Every other process closes `fd[0]` and writes events to `fd[1]`.
- Each event is **one text line, at most 255 bytes plus the newline, written with a single `write()` call**. Writes of at most `PIPE_BUF` bytes (4096 on Linux) are atomic, so lines from different processes never interleave. Mention this in the viva.
- Main must keep its write end open until all other writers have been reaped, then close it. Any writer end left open anywhere (for example in the dashboard) stops the logger from ever seeing EOF. Close unused ends in every child.
- Line format: `T=<tick> PID=<pid> EVT=<TYPE> PAT=<id|-> <free text>`.
- Event types: `ARRIVE, FORK, VENT_WAIT, VENT_ACQ, VENT_REL, BANKER_GRANT, BANKER_DENY, RUN, PREEMPT, IDLE, DONE, REAPED, READ_START, READ_END, WRITE_START, WRITE_END, CACHE, ERROR`.
- Output rule: only the dashboard writes to the terminal. With `--no-ui` the logger also echoes lines to stdout. No other process calls `printf`.

### 5.8 Dashboard

Standalone read-only process. It maps the shared memory and redraws every 200 ms using plain ANSI escape codes (alternate screen `\033[?1049h`, hide cursor `\033[?25l`, home `\033[H`, clear `\033[2J`). On exit or SIGINT it restores the cursor and the main screen. No ncurses.

Minimum terminal size 100 x 32. If smaller, show a "enlarge terminal" message.

Panels:
1. Header: tick, policy, running patient, simulation speed.
2. Patient table: id, state (colour coded), severity, remaining/burst, ventilator yes/no, allocation vector.
3. Gantt strip: the last 40 ticks, one character per tick showing the patient id (or `.` for idle). Preemptions are visible as a change of letter.
4. Ventilators: `[##### ]` style bar, used out of total.
5. Banker's: Total, Available, Allocation, Need table, and the current safe sequence (or `UNSAFE`).
6. Records DB: active readers, writer active yes/no, total reads, writes, corruption count.
7. Cache: FIFO and LRU frames, hits, misses, hit rate side by side.
8. Event log: the last 10 lines.

### 5.9 Scenario files

CSV with `#` comments and `@key=value` config lines:

```
@policy=srtf
@tick_ms=300
@ventilators=5
@total_blood=6
@total_icu=3
@total_surgery=2
@readers=3
@writers=2
@cache=4
# id,arrival,burst,severity,needs_vent, max_b,max_i,max_s, req1_b,req1_i,req1_s, req2_b,req2_i,req2_s, req2_at
1,0,5,2,0, 0,0,0, 0,0,0, 0,0,0, 0
```

- `classic.csv`: the manual's example. P1 (arrival 0, burst 5), P2 (1, 3), P3 (2, 8), P4 (3, 6), no resources, no ventilators.
- `er_rush.csv`: 10 patients, 7 need ventilators (only 5 exist), resource totals chosen so that at least one Banker's request is **denied as unsafe** and granted later. The agent must verify this by running the simulation and asserting it in a test. Include at least one patient with a second request (`req2`).
- `er_light.csv`: 5 patients, no contention.

The loader must validate: field counts, `req1 <= max`, `req1 + req2 <= max`, `max <= total`, patient count at most `MAX_PATIENTS`. Print a clear error and exit 1 on failure.

### 5.10 Standalone demos (Assignment 2)

- `demos/fork_sort.c`: read n integers, `fork()`. Child sorts descending and prints. Parent sorts ascending and prints, then `wait()`s.
- `demos/orphan.c`: parent exits right after `fork()`. Child sleeps, prints `getppid()` before and after, which changes to 1 (or the systemd PID).
- `demos/zombie.c`: child exits immediately. Parent sleeps 30 seconds without `wait()`. Print the PIDs and tell the user to run `ps -el | grep defunct` in another terminal.

---

## 6. Command line

```
./er_sim --scenario scenarios/er_rush.csv [options]

--policy srtf|fcfs     scheduling policy (overrides scenario)
--tick-ms N            tick length in ms (default from scenario, 300)
--fast                 same as --tick-ms 50
--no-ui                no dashboard, logger echoes to stdout
--no-sync              disable readers-writers semaphores (demo of corruption)
--cache-size N         frames for the FIFO/LRU caches
--seed N               random seed (default 42)
--demo cache           replay the manual's page reference string and exit
--help
```

Exit code 0 on success, 1 on bad input, 2 on internal error (stuck detector or failed system call).

---

## 7. Build system

`Makefile` targets:
- `make` builds `er_sim` and the three demos with `gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread` and links `-lrt`.
- `make test` builds and runs the three unit tests, exits non-zero on any failure.
- `make asan` builds with `-fsanitize=address,undefined -g` for debugging.
- `make clean` removes binaries, `hospital.log` and stale `/dev/shm/er_sim_shm`.

The code must compile with zero warnings.

---

## 8. demo.sh (Assignment 1)

`#!/bin/bash`, `set -euo pipefail`, a `trap` that kills children and removes shared memory on exit. Menu loop with `read -p` and `case`:

1. Build (`make`)
2. Run classic scenario, SRTF
3. Run classic scenario, FCFS
4. Compare SRTF vs FCFS (run both with `--no-ui --fast`, then compute and print average TAT and WT with `awk` or `$(( ))`)
5. Run ER rush scenario with dashboard
6. Readers-writers demo: run with sync, then with `--no-sync`, print both corruption counts
7. Cache demo (`--demo cache`)
8. Zombie demo (start `demos/zombie`, then show `ps -el | grep defunct`)
9. Orphan demo
10. Fork-sort demo
11. Run unit tests (`make test`)
12. Show process tree (`pstree -p $(pgrep -o er_sim)`)
13. Clean
0. Exit

It must also contain a small arithmetic block (sum, difference, product, quotient, remainder of two numbers entered by the user) as a nod to the manual's Assignment 1, labelled as such. Make the script executable (`chmod +x`).

---

## 9. Tests

Pure-function tests, no IPC. Each prints PASS or FAIL per case.

- `test_banker.c`
  - Golden case from the manual: Total (5,5,5), Allocation P0 (1,2,1), P1 (2,0,1), P2 (2,2,1), Need P0 (1,0,3), P1 (0,1,2), P2 (1,2,0). Expect Available (0,1,2), safe, sequence P1, P0, P2.
  - An unsafe state is detected.
  - A request above Need returns `ERROR_EXCEEDS_MAX`.
  - A request above Available returns `WAIT_UNAVAILABLE`.
  - A request that is available but leads to an unsafe state returns `WAIT_UNSAFE`.
- `test_sched.c` (manual's example: P1 (0,5), P2 (1,3), P3 (2,8), P4 (3,6))
  - SRTF: CT = P1 8, P2 4, P3 22, P4 14. TAT = 8, 3, 20, 11 (average 10.5). WT = 3, 0, 12, 5 (average 5.0).
  - FCFS: CT = P1 5, P2 8, P3 16, P4 22. TAT = 5, 7, 14, 19 (average 11.25). WT = 0, 4, 6, 13 (average 5.75).
- `test_cache.c`
  - Reference string above, 3 frames: FIFO 16 faults, LRU 15, Optimal 11.
  - An example where LRU beats FIFO and a Belady's anomaly case for FIFO (the string `1,2,3,4,1,2,5,1,2,3,4,5`: 9 faults with 3 frames, 10 faults with 4 frames).

To make `test_sched.c` possible, implement the scheduling simulation as a pure function `sched_simulate(procs, n, policy, out)` in `scheduler.c`. The live scheduler loop uses the same `srtf_pick` and `fcfs_pick` functions.

Also add an integration smoke test in `demo.sh` or a Makefile target: run `classic.csv` with `--no-ui --fast` and check the final summary matches the SRTF numbers above.

---

## 10. Quality bar

- No zombies left after a normal run (`ps` shows none).
- `/dev/shm/er_sim_shm` is gone after a normal exit and after Ctrl+C.
- Two runs with the same seed and scenario give the same schedule and the same Banker's decisions.
- Check every return value of `fork`, `pipe`, `shm_open`, `mmap`, `sem_*`, `read`, `write`. Print `perror` and exit 2 on failure.
- No busy loops without a sleep.
- Comments explain *why*, not just *what*.

---

## 11. Documentation to produce

`README.md`: how to build, how to run, flags, scenario format, process diagram.

`docs/report.md` using the lab manual's headings: Title, Aim, Objectives, Theory, Algorithm, Input, Output, Conclusion, FAQs. Include:
- Block diagram of processes, shared memory and the pipe.
- A flowchart each for: patient life cycle, SRTF tick, Banker's request, readers-writers.
- Mapping table from section 1.
- Screenshots of the dashboard, the SRTF vs FCFS comparison, the corruption demo and the zombie `ps` output.
- A "Design decisions" section explaining: processes instead of threads, semaphores in shared memory, the ventilator-first lock ordering, the single atomic pipe write, pure algorithm functions.
- FAQs (at least 10) such as: Why must the semaphore be in shared memory? Why does SRTF starve long jobs? Why can writes of at most PIPE_BUF bytes not interleave? What happens if a patient asks for more than its declared maximum? Why can Banker's refuse a request even when the resources are free? What is Belady's anomaly?

---

## 12. Build order (do each step, run `make test`, commit, then continue)

1. `common.h`, Makefile, scenario loader and validator.
2. Shared memory and semaphore setup/cleanup in main. Pipe and logger. Test with a dummy writer.
3. Pure functions with unit tests: `banker`, `fifo/lru/optimal`, `fcfs/srtf`. All three test files must pass before moving on.
4. Patient process and scheduler tick loop with `--no-ui`. Verify against the classic scenario numbers.
5. Banker's wrapper integrated into the patient life cycle.
6. Ventilator semaphore.
7. Records DB, doctors and nurses, `--no-sync`.
8. Cache wrapper hooked to doctor reads, `--demo cache`.
9. Dashboard.
10. Demos, `demo.sh`.
11. `er_rush.csv` tuning (confirm at least one unsafe denial), full-run checks from section 10.
12. README and report.

After each step, give the human a two-line summary: what works, what is next. Ask the human only when a decision changes the design. Do not add features outside this spec (no priority scheduling, no disk scheduling, no threads).

---

## 13. Definition of done

- `make` has zero warnings. `make test` passes.
- `./demo.sh` menu items 1 to 13 all work.
- Classic scenario matches the SRTF and FCFS numbers in section 9.
- ER rush scenario shows: a preemption, a ventilator wait, a Banker's unsafe denial followed by a later grant, concurrent readers, a writer taking exclusive access, and different FIFO and LRU hit rates.
- `--no-sync` produces a corruption count above zero, normal mode shows zero.
- No leftover zombies, processes or shared memory after exit or Ctrl+C.
- README and report are complete.
