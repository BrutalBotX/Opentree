# OpenTree

OpenTree is a disk usage viewer for Windows. It scans a folder (or a whole drive), shows you
where the space went, and keeps a history of how the numbers changed between scans. It is a Qt 6
desktop app.

## What it does

- Scans with the [Everything](https://www.voidtools.com/) index when Everything is running, and
  falls back to walking the file system when it isn't. You can turn the Everything path off in
  Settings if you prefer.
- Shows the results as a folder tree on the left and seven views on the right.
- Saves snapshots of a scan so you can compare "now" against an earlier point in time, and see
  per-folder history (which folders grew, which shrank).
- Finds duplicate files and junk files, and gives you a staged "maybe delete this" list. Nothing
  is ever removed without you confirming it in the Trash tab.
- Exports a report as HTML or PDF, with charts.

The views:

| Tab | What it is for |
| --- | --- |
| Graph | Node graph of folders and files. Force or tree layout, hover to focus a node. |
| Chart | Pie, bar, and treemap views of the selected folder. |
| Extensions | Space used per file type or per file family (video, images, ...). |
| Heatmap | Table of a folder's children, shaded by share of the parent. |
| Timeline | Saved snapshots, comparisons, and per-folder history. |
| Details | Sortable table of everything in the selected folder, with CSV export. |
| Duplicates | Finds identical files by size, then a quick hash, then a full hash. |
| Trash | The staged deletion list. Review it, then move things to the Recycle Bin. |
| Insights | Disk-full forecast, stale files, and junk candidates. |

## Requirements

- Windows 10 or later
- Qt 6.8.x (Widgets, Sql, Concurrent; WebEngineWidgets and WebChannel for the graph)
- CMake 3.24+
- Visual Studio 2022 Build Tools, or a MinGW toolchain that matches your Qt build

Graphics note: the graph tab uses Qt WebEngine, which needs the MSVC build. The MinGW build
skips WebEngine and shows a text list in that tab instead.

## Building

MSVC (recommended, full graph support):

```bat
build_msvc.bat
```

MinGW:

```bat
build_mingw.bat
```

Both scripts configure CMake into a build folder, build, and run `windeployqt` so the result is
runnable straight away. Edit the Qt path at the top of the script if yours is somewhere else.

Output:

- `build-msvc\OpenTree.exe`
- `C:\Users\<you>\otv2-build\OpenTree.exe` for MinGW (the script makes a junction without spaces,
  because `windres` cannot cope with spaces in paths)

If you build by hand instead:

```bat
cmake -S . -B build-msvc -G "NMake Makefiles" -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build-msvc
```

One thing worth doing after any build change: run the exe with only the system folders on
`PATH` (`set "PATH=C:\Windows\system32;C:\Windows"`). That is what double-clicking does, and it
catches missing deployed DLLs that a developer shell hides.

## Running

```bat
build-msvc\OpenTree.exe
```

Pick a drive or a folder and press Scan. Recent folders are kept under File > Recent Roots.

The scheduled snapshot task runs the app as:

```bat
OpenTree.exe --background-snapshot
```

## Tests

```bat
build-msvc\OpenTreeTests.exe
```

or `ctest --test-dir build-msvc -R core`. The suite covers path helpers, size formatting, staged
hash duplicate finding, the analysis services, report generation, the snapshot ledger with its
three-tier routing, the theme and table helpers.

## Themes

Four themes ship with the app, under View > Themes:

- **Graphite** (default) and **Light**: neutral greys. Graph folders are plain discs.
- **Blueprint**: dark, tinted blue.
- **Planets**: the space theme, where folders are drawn as lit planets.

A theme can be described in a folder next to the settings file (`themes/<name>/theme.json` plus an
optional `theme.qss`, and `graphStyle: "planets"` if you want the planet nodes). Reload Themes in
the View menu picks up changes without restarting.

## Where things live

| What | Where |
| --- | --- |
| Settings | `%LOCALAPPDATA%\OpenTree\OpenTree\opentree.ini` |
| Database (scans, snapshots, ledger) | `%APPDATA%\OpenTree\OpenTree\opentree.db` |
| Log | `opentree.log` next to the build output |
| Themes | `themes` under the settings folder |

Tools > "Open Settings Folder (opentree.ini)" and "Open Data Folder (database)" take you there.

## Command line

Handy when you want to check something without clicking around:

```bat
OpenTree.exe --test-scan <path>                  compare the Everything result with a file system walk
OpenTree.exe --find-duplicates <path> [minMB] [all]
OpenTree.exe --insights <path> [staleDays]       forecast, stale files, junk candidates
OpenTree.exe --test-ledger <path>                snapshot ledger and resolution tiers
OpenTree.exe --test-trash <path>                 virtual trash staging (writes nothing to disk)
OpenTree.exe --export-report <path> <out.html|out.pdf>
OpenTree.exe --render-window-preview <out.png> [path] [tab]
OpenTree.exe --dump-graph-html <path> <out.html> write the graph page to inspect in a browser
OpenTree.exe --smoke-graph <path>                check the lazy WebEngine start
OpenTree.exe --smoke-menu                        check that the menu drop-downs open
OpenTree.exe --smoke-subfolder <root> <child>    check that a child folder reuses a scanned root
```

## Known rough edges

- The graph loads Chromium the first time you open it, which costs a few hundred MB while it is
  up. It is released again when you leave the tab, but Chromium's in-process part stays loaded
  for the rest of the session.
- Scanning a folder inside an already scanned root reuses that root instead of adding a second
  tree. If the folder is newer than the scan, the root is rescanned.
- The installed Qt runtime may warn about missing optional plugins during deployment; they are
  not used by the app.
- The title bar icon depends on the Windows icon cache and sometimes needs a reinstall or a
  cache clear to show up.

## License

MIT. See `LICENSE`.
