# C — CORE

**Status:** C-2 complete (2026-10-14). Next phase in this document is C-3. The sequence runs B-1 before that.
**Resume:** The table of contents names the one phase that may be worked. If that id is not in this file, do not start here.

Threads, the loop, logging, and the store. Protocol work (JMAP, IMAP, the mail tables) is not in this document.

## Phases

One phase is one session. Do not start the next one in the same conversation.

| Phase | What | Kind | Status |
| --- | --- | --- | --- |
| C-1 | Log function | Easy | Complete |
| C-2 | Log thread and the line format | Medium | Complete |
| C-3 | Core thread and the command queue | Medium | Not started |
| C-4 | SQLite open and close | Medium | Not started |
| C-5 | Stop | Easy | Not started |
| C-6 | Start loads a profile | Medium | Not started |

Kind: **Easy** is one module and a Unity test. **Medium** is a thread or the database driver. **Hard** is unused in this document.

## How a session works

1. Read Resume in `PAVILLION_PLAN.md`, the Status column above, and the previous phase's Status, Accomplished, Lessons learned, and Handoff.
2. Re-read only this phase: Goal, Work items, Done means, Exit gate. Open the lock below when the phase cites it.
3. Ask the qualifying questions. Wait for approval before editing source.
4. Do this phase only. Check a work item `[x]` only after its check has passed. Defer with `[~]`, one line, and the phase it moves to.
5. Before marking complete, write Status, Accomplished, Lessons learned, and Handoff, and append the Working log. Then stop.
6. A sitting that ends early sets Status to **in progress**, names the last finished work item, and leaves the rest unchecked.

Leave Accomplished, Lessons learned, and Handoff off a phase that has not started.

## Lock

### Threads

| Thread | Owns | Does not touch |
| --- | --- | --- |
| Shell | The process, the WebView, argv | JSON, SQLite, curl, the keyring |
| Core | The profile, the store, the command queue | The WebView |
| Log | The open log file | Anything but the log queue |
| Worker | One per account, when sync exists | SQLite. A worker posts a result back to the core |

Hello world starts the shell, the core thread, and the log thread. No workers. The core thread is the only thread that opens the database or writes a profile. A worker never holds a `sqlite3 *`.

### Loop

The shell pushes a command and, when it needs a result now, waits on that command's reply. The core thread pops, runs, and replies. Events the UI did not ask for go to an event queue. The shell callback only enqueues. It does not draw. Commands inside the core are structs, not JSON.

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

Levels: `PAV_ERROR`, `PAV_WARN`, `PAV_INFO`, `PAV_DEBUG`. A line below the index `log_level` is discarded. Any thread may call it. The line format is millisecond UTC:

```
2026-10-14T17:04:01.123Z INFO  core  core ready
```

The clock is `CLOCK_REALTIME`. Three digits, zero-padded. The Activity view may show the same instant in local time as `HH:mm:ss.mmm`. The file stays this line. Before a profile is loaded, lines also go to stderr and to `<workdir>/logs/pavillion.log` when that directory exists. After load, the profile log is the file. `info` and above also go to stderr when stderr is a terminal.

When the log thread is running, `pav_log` queues the formatted line and returns. `pav_log_thread_shutdown` drains that queue and joins. A line logged while the thread is not running is written by the caller. The workdir file above is not opened yet.

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
} PavStoreVtable;
```

SQLite is the only driver in this document. `open` takes a filesystem path and creates an empty file, mode `0600`. It does not create the mail tables. `search` and migrations are a later document. Another engine fails `open` with an error that names it. No `dlopen` in this document.

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
**Status:** Not started

**Goal:** `pavillion_start` returns only after the core thread has set a state, and status is readable from another thread.

**Entry gate:** C-1 complete. C-2 may still be open. This phase may log with the C-1 function.

**Work items:**

- [ ] Start the core thread. It sets state `ready` and signals. `pavillion_start` then returns `0`.
- [ ] `pavillion_status` copies the struct under a lock.
- [ ] A stop command makes the core thread exit. This phase may join it directly. Idempotent stop is C-5.

**Done means:** A Unity test sees `ready` only after start has returned, and the thread is joined.

**Exit gate:** `make test` exits 0.

## C-4: SQLite open and close

**Kind:** Medium
**Status:** Not started

**Goal:** The vtable opens an empty SQLite file at a path and closes it. No mail schema.

**Entry gate:** C-3 complete.

**Work items:**

- [ ] Implement the SQLite row of `PavStoreVtable`: `open`, `close`, and `exec`.
- [ ] `open` creates the file mode `0600` and can `exec` a `SELECT 1`.
- [ ] `close` leaves the file on disk. A second `open` on the same path works.
- [ ] No other database client is linked.

**Done means:** A Unity test asserts the mode, `SELECT 1`, and a successful second open.

**Exit gate:** `make test` exits 0.

## C-5: Stop

**Kind:** Easy
**Status:** Not started

**Goal:** Stop joins the core thread and the log thread, and a second stop does nothing.

**Entry gate:** C-2 and C-3 complete.

**Work items:**

- [ ] `pavillion_stop` pushes stop, joins the core thread, and joins the log thread after the core has finished.
- [ ] A second `pavillion_stop` returns without error.
- [ ] `pavillion_start` after a stop succeeds. `pavillion_start` without a stop, while running, returns an error.

**Done means:** A Unity test covers stop, double stop, restart, and a refused double start.

**Exit gate:** `make test` exits 0.

## C-6: Start loads a profile

**Kind:** Medium
**Status:** Not started

**Goal:** The hello-world core: load the profile, open SQLite, log `core ready`, fill status.

**Entry gate:** B-4, B-5, C-2, C-4, and C-5 complete.

**Work items:**

- [ ] `pavillion_start` runs the B loader. First run creates the default profile. A broken active profile sets state `error` and does not exit the process.
- [ ] On success, open the SQLite path from the profile and log subsystem `core`, level `info`, message `core ready`.
- [ ] Status carries the profile `name` and `id`. For the default profile the name is `Default`.
- [ ] An engine other than `sqlite` is state `error` and the error text contains the engine name.

**Done means:** On a fresh temp directory, status name is `Default`, state is `ready`, the log contains `core ready`, and the SQLite file exists mode `0600`.

**Exit gate:** `make test` exits 0.

## Working log

- 2026-10-14 C-1: `pav_log` writes one millisecond-UTC line under a mutex. Debug is dropped at the default info level. A bad `num_args` logs an error and does not format the call.
- 2026-10-14 C-2: The log thread writes queued lines in call order. Shutdown flushes the queue and joins. A line before the thread starts is written by the caller.
