# Hospital ER Simulator design

The program follows the supplied specification as a Linux C11 multi-process system. The admission process owns one POSIX shared-memory region and one pipe, forks dedicated logger, scheduler, dashboard, reader, writer, and patient processes, and performs all cleanup. Process-shared unnamed semaphores protect the Banker's state, records database, caches, and patient tick handshakes.

The scheduling, Banker's safety/request logic, and page-replacement algorithms remain pure array-based functions. Their live wrappers only translate shared state, lock it, invoke the pure operation, and publish an atomic event. This keeps the three required unit-test binaries deterministic and makes their algorithms suitable for a viva explanation.

The implementation is delivered in the requested twelve-stage order but grouped into two repository commits: stages 1–6 establish parsing, IPC, pure algorithms, patients, scheduling, resources and ventilators; stages 7–12 add records, cache integration, dashboard, demonstrations, tuned scenarios, testing and documentation. Validation includes warning-free builds, unit tests, classic-policy smoke checks, ER-rush event assertions, cleanup checks, and repeatability checks.
