# Everything Integration Plan

## Problem

The Everything SDK path exists in code but has never actually scanned anything.

- `ScanService::performScan()` always calls `collectFilesystemFiles()` and hard-codes
  `result.usedEverything = false`. `EverythingClient` is constructed and injected but
  never queried, so the app has only ever used filesystem scanning.
- `EverythingClient` has correctness bugs that would make it fail even if wired in.

## Diagnosis (root causes)

1. **Client is never called.** `ScanService` ignores `m_everythingClient`.
2. **Availability is misdetected.** `isAvailable()` returns `m_loaded`, which only means
   `Everything64.dll` loaded. The DLL loads even when the Everything service is not
   running; queries then fail with `EVERYTHING_ERROR_IPC` (2). `ensureEverythingRunning()`
   short-circuits on `isAvailable()` and therefore never actually starts Everything.
3. **Wrong search expression.** Search is
   `"parent:^<root>\\"` with `SetRegex(TRUE)`. `parent:` is not a valid Everything
   search function, and with regex on the Windows path backslashes are treated as regex
   escapes. This yields wrong or empty results. The supported pattern is
   `SetMatchPath(TRUE)` + a plain (non-regex) path search, then keep results whose full
   path starts with the root.
4. **No `Everything_Reset()` between queries.** Search/result state leaks across calls.
5. **Hard-coded DLL name.** Always `Everything64.dll`; wrong on x86/ARM builds.
6. **Two-pass query.** `queryRecursive` and `queryFoldersRecursive` each run the whole
   query separately; a scan needs both, so it would scan Everything twice.
7. **Silent caps.** `SetMax(1000000)` truncates large roots with no pagination and no
   signal to the caller.
8. **No fallback/reporting.** A failed Everything query returns empty rather than falling
   back to filesystem scanning, and nothing tells the user which engine ran or why.

Environment verified on this machine: Everything **1.4.1.1032** running at
`C:\Program Files (x86)\Everything\Everything.exe`, service `Everything` present, so the
integration is testable here.

## Design decisions

- **Everything is the preferred scan engine when usable, with automatic filesystem
  fallback.** No user action required, but a setting can force filesystem-only.
- **Keep the `ScanResult` contract unchanged.** The Everything path just produces
  `folders` + `files` and feeds the existing `buildTreeResult()`. This preserves the
  documented handoff type and the cache/db pipeline.
- **Client-side exclusion filtering.** Everything indexes `$Recycle.Bin`, `System Volume
  Information`, etc.; the existing `excludedPatterns()` list is applied to results so
  Everything scans match filesystem scans.
- **Single combined query** returning files and folders together.
- **Paged retrieval** using `Everything_GetTotResults()` + `SetOffset`/`SetMax` so large
  roots are not silently truncated.
- **Thread-safety:** the client is only used from the scan worker; add a mutex around
  SDK state as cheap insurance because `ensureEverythingRunning` can run from the UI
  thread.

## Implementation steps

### 1. Harden `EverythingClient` (`src/integrations/EverythingClient.*`)

- Split state into `m_libraryLoaded` (DLL present) and `m_serviceAvailable` (IPC works).
  `isAvailable()` means both.
- Add `bool testConnection(QString *error)` that runs a trivial query and inspects
  `Everything_GetLastError()` (treat `EVERYTHING_ERROR_IPC` as "not running").
- Add architecture-aware DLL name selection (`Everything64/32/ARM64/ARM.dll`) and search
  next to the executable (and optionally the configured Everything install dir).
- `ensureEverythingRunning()`: if IPC not up, and a configured `Everything.exe` exists,
  launch it, then poll `testConnection()` with backoff.
- `runQuery()`:
  - lock a mutex,
  - `Everything_Reset()`,
  - `SetMatchPath(TRUE)`, `SetRegex(FALSE)`, search = native root path,
  - request `FULL_PATH_AND_FILE_NAME | SIZE`,
  - sort by path,
  - page through `GetTotResults()` collecting files/folders,
  - filter to paths that start with the normalized root,
  - return a real success/failure and a descriptive error.
- Add `queryRoot(rootPath, *files, *folders, *error)` single-pass API; keep the two
  thin wrappers for compatibility.
- Add a destructor calling `Everything_CleanUp()` and unloading the library.

### 2. Wire Everything into `ScanService` (`src/services/ScanService.*`)

- `performScan()`:
  1. resolve whether to use Everything (setting + client availability),
  2. if yes: call the combined query, apply exclusions, build the tree, set
     `usedEverything = true`, emit progress ("Querying Everything index"),
  3. on any failure: log the reason, emit a progress/status note, fall back to the
     existing filesystem scan with `usedEverything = false`.
- Add `scanEngineUsed` / reason reporting so the UI can show it.
- Keep static helpers unchanged.

### 3. Config (`src/services/ConfigService.*`)

- Add `Scanning/UseEverything` (bool, default `true`).
- Add getter/setter `useEverything()` / `setUseEverything()`.
- Default `Scanning/EverythingExecutablePath` to the common install location when unset
  (auto-detect `C:\Program Files\Everything\Everything.exe` and the x86 path).

### 4. UI (`src/app/MainWindow.*`, `src/app/AppController.*`)

- Rename Tools action "Locate Everything SDK" -> "Locate Everything Executable…".
- Add Tools items:
  - checkable **"Use Everything when available"** bound to the config setting,
  - **"Test Everything Connection"** that reports availability/IPC result in a dialog
    and the status bar.
- Status line already prints engine via `usedEverything`; enrich the fallback message.
- `AppController`: implement the new handlers and a `everythingLocationRequested` flow
  that immediately tests the chosen executable.

### 5. Diagnostics / CLI test hook (`src/main.cpp`)

- Add `--test-everything <path>` mode: initialize `EverythingClient`, print
  availability, service state, elapsed ms, and file/folder counts, then exit. This gives
  a headless way to verify without opening the GUI.

### 6. Docs

- Update `PROJECT_STATUS.md`, `github/README.md`, `github/CHANGELOG.md` to move Everything
  out of "deferred" and describe the fallback behavior.
- Add a short `EVERYTHING_PLAN.md` (this file) implementation-status section.

## Testing plan

1. Build the MSVC target via `build_msvc.bat` (Qt 6.8.0 msvc2022_64).
2. Headless engine check:
   `build-msvc\OpenTree.exe --test-everything "D:\old stuff\Others\Experiments\Opentreev2\src"`
   - expect Everything reported available and non-zero file/folder counts.
3. Cross-check counts against a filesystem scan of the same small folder (add a
   `--test-filesystem <path>` companion or reuse existing static scan in the test mode)
   and confirm they agree once exclusions are applied.
4. GUI smoke: scan a folder, confirm status says "via Everything", tree/details populate,
   cached reload still works.
5. Fallback test: set "Use Everything when available" off -> status says "via
   filesystem"; also validate graceful failure when the service is unreachable.
6. Large-root sanity: scan a drive root and confirm pagination does not truncate.

## Implementation Status (done)

Code changes are in place:

- `src/integrations/EverythingClient.h/.cpp` - rewritten: separate library vs service state,
  IPC-aware `testConnection()`, architecture-aware DLL name, `Everything_Reset()` per query,
  single combined `queryRoot()`, paged retrieval, prefix filtering, mutex, RAII cleanup.
- `src/services/ScanService.h/.cpp` - `performScan()` now tries Everything first, applies
  exclusions, builds the tree, sets `usedEverything = true`, and falls back to the
  filesystem with a logged reason. Config is read on the UI thread and passed to the
  worker.
- `src/services/ConfigService.h/.cpp` - `Scanning/UseEverything` (default true) plus
  `resolvedEverythingExecutablePath()` auto-detection.
- `src/app/MainWindow.h/.cpp` + `src/app/AppController.h/.cpp` - Tools menu now has a
  checkable "Use Everything when available", "Locate Everything Executable...", and
  "Test Everything Connection"; checkbox reflects saved config.
- `src/main.cpp` - new headless `--test-scan <path>` mode printing Everything vs
  filesystem results.

## End-to-end verification (built and run)

Toolchain provisioned for this validation (no Visual Studio needed):

- Qt 6.8.0 MinGW kit + GCC 13.1 via `aqtinstall` (in `%USERPROFILE%\Qt`)
- CMake 4.4.3 + Ninja via pip
- `build_mingw.bat` creates `%USERPROFILE%\otv2-build\OpenTree.exe`. The build runs
  through a no-space junction (`%USERPROFILE%\otv2`) because `windres` cannot handle
  spaces in include paths.

`OpenTree.exe --test-scan <path>` compares the Everything query against a filesystem
scan:

| Root | Everything | Filesystem | Result |
| --- | --- | --- | --- |
| `...\Opentreev2\src` | 56 files, 9 folders, 326204 B (14 ms) | 56 files, 9 folders, 326204 B | exit 0, exact match |
| `C:\Windows\System32` | 19638 files, 2228 folders (305 ms) | 19536 files, 1590 folders (2182 ms) | differs (see below) |

- The permission-clean source tree matches exactly, so the query and tree build are
  correct.
- System32 differs because Everything indexes restricted items `QDirIterator` cannot
  stat, and returns empty folders that `buildTreeResult` omits; Everything was ~7x
  faster and no out-of-root results were returned.
- Auto-start: after stopping the interactive Everything client, a scan detected the IPC
  failure, launched the configured client itself, and completed via Everything (exit 0).
- Fallback: with the client stopped and the executable path forced invalid, the scan
  reported the IPC error and the missing executable, then completed with a filesystem
  scan (`usedEverything=false`, exit 2).
- GUI smoke test: window opens and closes cleanly with the new Tools menu entries.

## SDK-level verification (Python ctypes)

The Qt/MSVC toolchain is not installed in the current environment, so the C++ app could
not be rebuilt here. The SDK call sequence and search syntax were instead validated
directly against `third_party/dll/Everything64.dll` with a ctypes probe mirroring the C++
implementation:

| Root | Everything | os.walk | Outside-root |
| --- | --- | --- | --- |
| `...\Opentreev2\src` | 56 files, 9 folders, 325152 B | 56 files, 9 folders, 325152 B | 0 |
| `C:\Windows\System32` | 19639 files, 2228 folders | 19612 files, 2223 folders | 0 |
| `D:\old stuff` | 288336 files, 37209 folders | 288282 files, 37189 folders | 0 |

- Exact match on the source folder, including a path containing spaces
  (`D:\old stuff\...`), confirming the quoted non-regex search.
- Paged retrieval (65,536 per page, 5 pages) reproduced the full 288,336-file result
  byte-for-byte, validating the pagination loop.
- The larger trees differ slightly from `os.walk` because Everything indexes items that
  `os.walk` cannot stat (permissions), and Everything does not descend reparse points the
  same way; no returned path fell outside the requested root.
- With only the Everything *service* running (session 0) and no interactive client, the
  SDK returns error 2 (`EVERYTHING_ERROR_IPC`); the new detection correctly reports
  "not running" whereas the old `isAvailable()` reported success.

## Not yet re-verified

- The MSVC/WebEngine build (`build_msvc.bat`) was not re-run here because Visual Studio
  is not installed in this environment; the MinGW build compiled the same sources
  cleanly, so the changes are expected to build under MSVC as well.
- Interactive GUI verification of the "Use Everything when available" checkbox toggling
  the status text was not performed (headless validation only).

## Risks / notes

- Everything SDK query functions share global state and are not re-entrant; the mutex +
  single-scan guard keeps us safe, but no concurrent scans.
- The SDK exposes limited metadata (no owner/attributes). Scan results will carry
  path/size; details panel already degrades gracefully with `-` for missing values.
- The `github/` folder is a stale export; all source changes go in root `src/`.
