# C — CORE

**Status:** C-6 complete (2026-10-14). No further phase in this document. The index names the next phase.
**Resume:** The index ([PAVILLION_PLAN.md](../PAVILLION_PLAN.md)) names the one phase that may be worked. If that id is not in this file, do not start here.

Threads, the loop, logging, and the store. Protocol work (JMAP, IMAP, the mail tables) is not in this document. Later additions to the thread table are the net threads (one per server endpoint) and work pool ([M-SYNC.md](M-SYNC.md)), the script thread ([R-RULES.md](R-RULES.md)), and the event thread ([G-LAYERS.md](G-LAYERS.md) G-2). Note (2026-10-16): [H-LOGGING.md](H-LOGGING.md) H-1 starts the log thread from `pavillion_start`. Migrations and the schema are [L-STORE.md](L-STORE.md).

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| C-1 | Log function | Easy | Complete |
| C-2 | Log thread and the line format | Medium | Complete |
| C-3 | Core thread and the command queue | Medium | Complete |
| C-4 | SQLite open and close | Medium | Complete |
| C-5 | Stop | Easy | Complete |
| C-6 | Start loads a profile | Medium | Complete |

Kind: **Easy** is one module and a Unity test. **Medium** is a thread or the database driver. **Hard** is unused in this document.

## How a session works

1. Read Resume in `PAVILLION_PLAN.md`, the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Work items, Done means, Exit gate. Open the lock below when the phase cites it.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, and append the Working log. Before that, update `ui/src/help/pages.js` for what this phase changed: augment, replace, fix, or extend the user page, or add a Developer section when the user page has nothing new. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

### Threads

| Thread | Owns | Does not touch |
| --- | --- | --- |
| Shell | The process, the WebView, argv | JSON, SQLite, curl, the keyring |
| Core | The profile, the store, the command queue | The WebView |
| Log | The log queue, the ring, and from H-4 the profile's `log.db` connection ([H-LOGGING.md](H-LOGGING.md) Lock) | The profile store, and anything but the log queue |
| Worker | The work pool, net threads, and script worker added later (M, R) | SQLite. A worker posts a result back to the core |
| Store writer | From L-1: the only read-write connection to the profile store and the write queue ([L-STORE.md](L-STORE.md) Lock). `log.db` is the log thread's, not this thread's | The profile, curl, `log.db` |

Hello world starts the shell, the core thread, and the log thread. No workers. The core thread is the only thread that writes a profile. Until L-1 it is also the only thread that opens the database. From L-1, the store writer owns the only write connection to the profile store and the core thread uses the read pool. From H-4, the log thread owns `log.db`, the one exception ([L-STORE.md](L-STORE.md) Lock). A worker never holds a `sqlite3 *`.

Every thread Pavillion creates sets a 512 KB stack (`pthread_attr_setstacksize`) before `pthread_create`. The platform default (8 MB on Linux) is not used. Lua's cap is a heap cap ([R-RULES.md](R-RULES.md)), not a stack size. A Unity test in L-1 records the attribute the store writer was created with.

### Loop

The shell pushes a command and, when it needs a result now, waits on that command's reply. The core thread pops, runs, and replies. Events the UI did not ask for go to an event queue. The shell callback only enqueues. It does not draw. Until G-2 the queue carries struct commands. From G-2 the external contract is JSON (`pavillion_call`), and the core queue carries the command name plus a parsed `json_t *` that the dispatcher frees. Handlers do not grow a second struct ABI beside that. The hello-world structs (`PavillionStatus`, `PavShellPrefs`) stay until G-8 deletes them.

`pavillion_stop` pushes stop and joins the core thread, which joins workers and then the log thread. Stop is idempotent. A second `pavillion_start` without a stop returns an error.

### Public functions

```c
int  pavillion_start(const char *workdir, char *err, size_t err_len);
void pavillion_stop(void);
int  pavillion_status(PavillionStatus *out);

typedef struct PavillionStatus {
    char profile_name[256];
    char profile_id[64];
    char state[32];   /* "ready" or "error" */
    char error[512];
} PavillionStatus;
```

`pavillion_start` returns only after the state is `ready` or `error`. The core thread stays alive on error so the window can show `error`. The process does not exit itself.

### Logging

```c
void pav_log(const char *subsystem, int level, int num_args, const char *fmt, ...);
```

`num_args` is the number of format specifiers in `fmt`, counted the way Hydrogen counts them: `%%` is literal, and a `*` width is not an extra argument. A mismatch logs an error and returns without formatting `fmt`.

Levels: `PAV_ERROR`, `PAV_WARN`, `PAV_INFO`, `PAV_DEBUG`, `PAV_TRACE` (0 to 4). Every level is captured; nothing is discarded by level. The index `log_level` is only the Log view's default level filter. The ring and `log.lines` carry every level ([H-LOGGING.md](H-LOGGING.md) Lock, Levels). Any thread may call it. The line format is millisecond UTC:

```
2026-10-14T17:04:01.123Z INFO  core  core ready
```

The clock is `CLOCK_REALTIME`. Three digits, zero-padded. The Activity view may show the same instant in local time as `HH:mm:ss.mmm`. Exports use this line. From H-4 (this Lock written 2026-10-19; until then the code writes `logs/`), there are no log files: `log.db` stores the line's fields and is the record ([H-LOGGING.md](H-LOGGING.md) Lock). A line logged before any workdir exists goes to stderr at every level, because stderr is the only record. From then on nothing goes to stderr at any level (Andrew, 2026-10-19, replacing the `warn` and `error` copy), except every level while `log.db` cannot be opened. Before the profile attaches, lines wait in the ring; after attach, `log.db` and the ring hold every line.

When the log thread is running, `pav_log` queues the formatted line and returns. `pav_log_thread_shutdown` drains that queue and joins. A line logged while the thread is not running is written by the caller. From H-4, `pavillion_start` opens no log file; the log thread opens `log.db` when the profile attaches. From H-4, `pav_log` at `error` waits for that line's commit for about 250 ms at most, then returns anyway.

```c
int  pav_log_thread_start(void);
void pav_log_thread_shutdown(void);
```

Never pass a password, a token, an `Authorization` header, or a message body to `pav_log`. The Hydrogen check that the number of format arguments matches the format string is part of C-1.

### Store

```c
typedef struct PavStore PavStore;

typedef struct PavStoreVtable {
    int  (*open)(PavStore *store, const char *path_or_conn, char *err, size_t err_len);
    void (*close)(PavStore *store);
    int  (*exec)(PavStore *store, const char *sql, char *err, size_t err_len);
    int  (*migrate)(PavStore *store, char *err, size_t err_len);
} PavStoreVtable;
```

SQLite is the only driver in this document. `open` takes a filesystem path and creates an empty file, mode `0600`. It does not create the mail tables and it sets no PRAGMAs. `migrate` is L-1: the store writer applies the L Lock PRAGMAs, runs embedded migrations, then opens the read pool. C-6 calls it after `open` and before the activity registry attaches. Failure is state `error` and does not attach. Another engine fails `open` with an error that names it. No `dlopen` in this document.

## C-1: Log function

**Kind:** Easy
**Status:** Complete (2026-10-14). No variances. The lock's `pav_log` signature now includes `num_args`, which this phase's check requires.

**Goal:** `pav_log` writes one formatted line under a mutex. No thread yet.

**Entry gate:** A-2 complete.

**Work items:**

- [x] Format the lock's line, including millisecond UTC time, level, and subsystem.
- [x] Drop a line below the current level. Default level is `info`.
- [x] Copy the Hydrogen `num_args` check: the count passed by the caller matches the format string, and a mismatch is itself logged at `error` without crashing.
- [x] A Unity test writes to a temp file via a test-only set-output call and asserts one line. A fixture string that looks like a password is not in that file because the test never passes it.

**Done means:** The Unity test asserts the exact line format and the level filter.

**Exit gate:** `make test` exits 0. Passed: 4 tests, 0 failures. The info line is `INFO  core  core ready` after a millisecond-UTC stamp. A debug line is absent. `%%` becomes `%`. A bad `num_args` logs `ERROR log  format specifier count mismatch` and does not format the call.

**Accomplished:** `pav_log` and `pav_log_set_output` are in `core/src/pavillion.c`. The specifier count follows Hydrogen, including `%%` as a literal. The default level is info.

**Lessons learned:** Hydrogen prints a warning and still formats a mismatched call, which can crash. This function logs the error and returns before `vsnprintf`. The copied counter does not treat a `*` width as an argument. That matches Hydrogen.

**Handoff:** C-2 makes `pav_log` push the formatted bytes and return. The log thread writes them. Keep `pav_log_set_output`. Do not start the core thread in C-2.

## C-2: Log thread and the line format

**Kind:** Medium
**Status:** Complete (2026-10-14). No variances.

**Goal:** Callers enqueue. A log thread writes the file in order.

**Entry gate:** C-1 complete.

**Work items:**

- [x] `pav_log` pushes bytes and returns. The log thread writes them.
- [x] Lines leave the file in call order across two threads.
- [x] Shutdown of the log thread flushes the queue.

**Done means:** A Unity test from two threads produces an ordered file and no lost line after shutdown.

**Exit gate:** `make test` exits 0. Passed: 5 tests, 0 failures. Two threads alternate forty lines. After shutdown the file is `T0-00`, `T1-00`, `T0-01`, `T1-01`, and so on, with no line missing.

**Accomplished:** `pav_log_thread_start` and `pav_log_thread_shutdown` run one writer thread. `pav_log` formats the line, queues those bytes, and returns. Shutdown drains the queue and joins. The C-1 tests now start that thread, so the format checks go through the queue.

**Lessons learned:** A line logged while the thread is not running is written by the caller. Otherwise it would sit in a queue with no reader. Shutdown is safe to call when the thread is already stopped.

**Handoff:** B-1 is next in the sequence. C-3 starts the core thread and may log with `pav_log`. It does not have to start the log thread. C-5 is what joins it from `pavillion_stop`. Do not open the workdir log file here.

## C-3: Core thread and the command queue

**Kind:** Medium
**Status:** Complete (2026-10-14). No variances.

**Goal:** `pavillion_start` returns only after the core thread has set a state, and status is readable from another thread.

**Entry gate:** C-1 complete. C-2 may still be open. This phase may log with the C-1 function.

**Work items:**

- [x] Start the core thread. It sets state `ready` and signals. `pavillion_start` then returns `0`.
- [x] `pavillion_status` copies the struct under a lock.
- [x] A stop command makes the core thread exit. This phase may join it directly. Idempotent stop is C-5.

**Done means:** A Unity test sees `ready` only after start has returned, and the thread is joined.

**Exit gate:** `make test` exits 0. Passed: 17 tests, 0 failures. After `pavillion_start` returns, status is `ready` and `pav_core_thread_alive` is 1. A second thread copies that status. After `pavillion_stop`, the thread is joined and status is still readable. The older test still calls stop twice.

**Accomplished:** `pavillion_start` creates the core thread and waits until that thread publishes state `ready`. Stop pushes `CORE_CMD_STOP` and joins the thread. Status copies `PavillionStatus` under the core lock. `workdir` is still ignored.

**Lessons learned:** Start already returns -1 with `already running` if the thread is alive, so a second call cannot leak a thread. C-5 is the test of that refusal and of start after stop. A second stop sees the thread gone and returns. Status before the first start stays `ready` with an empty name, which is what the shell shows until A-5 calls start. This phase does not start or join the log thread, and it does not log `core ready`.

**Handoff:** C-4 adds the SQLite vtable: open, close, and exec. Do not open a database from `pavillion_start`. C-5 joins the log thread from `pavillion_stop`.

## C-4: SQLite open and close

**Kind:** Medium
**Status:** Complete (2026-10-14). No variances.

**Goal:** The vtable opens an empty SQLite file at a path and closes it. No mail schema.

**Entry gate:** C-3 complete.

**Work items:**

- [x] Implement the SQLite row of `PavStoreVtable`: `open`, `close`, and `exec`.
- [x] `open` creates the file mode `0600` and can `exec` a `SELECT 1`.
- [x] `close` leaves the file on disk. A second `open` on the same path works.
- [x] No other database client is linked.

**Done means:** A Unity test asserts the mode, `SELECT 1`, and a successful second open.

**Exit gate:** `make test` exits 0. Passed: 18 tests, 0 failures. The file is mode `0600`. `SELECT 1` returns 0. A second open on the same path works. `ldd` shows `libsqlite3.so.0` and no other database client.

**Accomplished:** `pav_store_open`, `pav_store_exec`, and `pav_store_close` dispatch through `PavStoreVtable`. The SQLite row uses `sqlite3_open_v2` and `sqlite3_exec`. `db2` fails before SQLite is called.

**Lessons learned:** `sqlite3_open` creates the file with the process umask, so a successful open then `chmod`s it to `0600`. A failed open removes the file only when that call created it. `exec` does not return rows. Open does not create mail tables. The shell link in `src-tauri/build.rs` includes `sqlite3` so the archive can resolve those symbols.

**Handoff:** C-5 makes `pavillion_stop` join the log thread after the core thread. Do not open a database from anywhere but the core thread.

## C-5: Stop

**Kind:** Easy
**Status:** Complete (2026-10-14). No variances.

**Goal:** Stop joins the core thread and the log thread, and a second stop does nothing.

**Entry gate:** C-2 and C-3 complete.

**Work items:**

- [x] `pavillion_stop` pushes stop, joins the core thread, and joins the log thread after the core has finished.
- [x] A second `pavillion_stop` returns without error.
- [x] `pavillion_start` after a stop succeeds. `pavillion_start` without a stop, while running, returns an error.

**Done means:** A Unity test covers stop, double stop, restart, and a refused double start.

**Exit gate:** `make test` exits 0. Passed: 19 tests, 0 failures. A second start while running returns -1 and the error contains `already running`. After stop, `pav_log_thread_start` succeeds, and a later start reaches `ready` again. A second stop does not crash.

**Accomplished:** Stop enqueues `CORE_CMD_STOP`, joins the core thread, then calls `pav_log_thread_shutdown`. The line logged before stop is in the file, so the queue was drained.

**Lessons learned:** The log thread is joined only after the core thread has returned, so a line the core queued is not left behind. A second stop finds the core already stopped and still calls log shutdown, which does nothing when that thread is already down. The refused second start was already returned by C-3. This phase is the test of it.

**Handoff:** C-6 runs the B loader on the core thread, opens SQLite, and publishes the profile name. Stop already joins both threads.

## C-6: Start loads a profile

**Kind:** Medium
**Status:** Complete (2026-10-14). No variances.

**Goal:** The hello-world core: load the profile, open SQLite, log `core ready`, fill status.

**Entry gate:** B-4, B-5, C-2, C-4, and C-5 complete.

**Work items:**

- [x] `pavillion_start` runs the B loader. First run creates the default profile. A broken active profile sets state `error` and does not exit the process.
- [x] On success, open the SQLite path from the profile and log subsystem `core`, level `info`, message `core ready`.
- [x] Status carries the profile `name` and `id`. For the default profile the name is `Default`.
- [x] An engine other than `sqlite` is state `error` and the error text contains the engine name.

**Done means:** On a fresh temp directory, status name is `Default`, state is `ready`, the log contains `core ready`, and the SQLite file exists mode `0600`.

**Exit gate:** `make test` exits 0. Passed: 21 tests, 0 failures. A fresh directory comes back `Default`, `default`, `ready`, with `default.sqlite` mode `0600` and the line `INFO  core  core ready`. A missing workdir, a file used as a directory, a profile that is `{`, and engine `db2` all leave the core thread alive with state `error`. `db2` does not create a SQLite file. The `{` profile is not replaced.

**Accomplished:** The core thread calls `pav_config_ensure`, `pav_config_load`, `pav_profile_check_engine`, `pav_config_prepare`, and `pav_store_open`. Start returns 0 after that thread publishes `ready` or `error`. The earlier start tests now pass a fresh directory.

**Lessons learned:** The engine check runs before prepare, so `db2` does not create the log directory or the database file. NULL workdir publishes `missing workdir` and creates nothing. `core ready` is logged only after SQLite is open. The Unity test captures that line through its own output file. A-5 opens the profile log for append when no output is set, so a normal launch writes the same line to `logs/default.log`.

**Handoff:** A-5 passes the working directory into `pavillion_start` and calls `pavillion_stop` when the window quits. The Unity test already expects `Default` and `core ready`.

## Working log

- 2026-10-14 C-1: `pav_log` writes one millisecond-UTC line under a mutex. Debug is dropped at the default info level. A bad `num_args` logs an error and does not format the call.
- 2026-10-14 C-2: The log thread writes queued lines in call order. Shutdown flushes the queue and joins. A line before the thread starts is written by the caller.
- 2026-10-14 C-3: `pavillion_start` returns after the core thread publishes `ready`. Status is copied under a lock. Stop pushes a command and joins. A second stop does nothing.
- 2026-10-14 C-4: The SQLite vtable opens a file mode 0600, runs `SELECT 1`, and opens the same path again. No other database client is linked.
- 2026-10-14 C-5: Stop joins the core thread, then the log thread. A second stop does nothing. Start after stop works. A second start while running is refused.
- 2026-10-14 C-6: Start loads the profile on the core thread, opens SQLite, and logs `core ready`. A broken profile or `db2` sets state `error` and leaves the thread alive.
- 2026-10-19 Lock change for H-4 (logging redesign, written before code): the Log thread owns the profile's `log.db`, and from H-4 there are no log files. The line format is unchanged and is what exports and the one-time import use. Before any workdir, stderr only. Before attach, stderr and the ring. After attach, `warn` and `error` copy to a terminal. See [H-LOGGING.md](H-LOGGING.md) Lock.
- 2026-10-19 Andrew approved the H-4 recommendations. stderr: every level before any workdir exists, then `warn` and `error` only, at all times. An `error` line waits for its `log.db` commit for about 250 ms at most. See [H-LOGGING.md](H-LOGGING.md) Lock.
- 2026-10-19 Andrew: logs go to the database only. After a workdir exists no level is copied to stderr; stderr keeps only the pre-workdir lines, the lines while `log.db` cannot open, and the second-launch line. See [H-LOGGING.md](H-LOGGING.md) Lock.
- 2026-10-19 Logging: `PAV_TRACE` added; every level is captured and `log_level` is only the view default (H-4 Lock).
