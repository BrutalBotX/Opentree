# OpenTree

OpenTree is a Qt 6 desktop disk-usage explorer for Windows focused on large-folder analysis, timeline snapshots, and multiple visual views of storage usage.

It is designed as a Windows-first visual storage analysis tool with a TreeSize-like workflow, but with built-in timeline/snapshot analysis and richer experimental views such as graph, heatmap, chart workspace, and multi-root navigation.

## Current Features

- Multi-root disk tree browsing in one app session
- Everything SDK index scanning (preferred) with automatic filesystem fallback
- Cached root reload path: previously scanned roots can show cached folder data first while a fresh scan runs in the background
- Explorer-style path/address entry for graph and chart workspaces
- Details pane for files and folders
- Details tab: sortable per-item table with inline % -of-parent bars, a flat "include subfolders" mode, and CSV export
- Toolbar drive selector with free/used capacity bars
- Graph view with Force/Tree layout toggle, fit/zoom controls, planetary folder nodes, file diamonds and a starfield background
- Graph right-click context menu for cross-tab navigation (Pie, Bars, Treemap, Extensions, Heatmap)
- Chart workspace:
  - Pie with non-overlapping outside labels
  - Bars that fit the available rows
  - Treemap with recursive depth control and per-level Other aggregation
  - Optional free-space slice when scanning a drive root
- Heatmap view
- Extensions view with grouping by extension or by file family
- Timeline and snapshot comparison
- Per-folder timeline: the Timeline tab shows the selected folder's recorded sizes per snapshot, straight from the Merkle-style structural ledger
- Merkle-style structural ledger with three-tier resolution routing (high resolution, macro, blacklist)
- Duplicates tab: staged size -> partial hash -> full hash duplicate detection with a skip-system-folders guard
- Insights tab: disk-full forecast from the saved snapshot trend, stale-file detection, and junk candidates with one-click staging
- Reports: export the current scan as HTML (with charts) or PDF (summary cards, folder bar
  chart, file-type donut, paginated tables). The PDF is written with QtGui's QPdfWriter, so
  no Qt PrintSupport dependency is needed.
- Virtual trash: staging is an explicit "Stage for Deletion" action in every context menu
  (tree, details pane, charts, details table, heatmap, duplicates, graph). The Trash tab
  refreshes automatically and is review-only: nothing is deleted until the user confirms
  moving items to the Windows Recycle Bin.
- First-run Everything prompt: if Everything is not running, OpenTree offers to open the
  voidtools download page or start an existing install. The SDK is the only engine; the app
  never launches Everything by itself.
- Unified Settings dialog (theme, scanning, snapshots, graph, deduplication) with live apply
- Tray residency with notifications and optional close-to-tray behaviour
- Snapshot manager dialog with tabulated snapshot rows and delete support
- Background snapshot mode
- Theme support with built-in light/dark themes and reloadable external themes
- Core test suite (`OpenTreeTests`, `ctest` target `core`)
- Windows installer scaffold with Inno Setup

## Tech Stack

- C++17
- Qt 6 Widgets
- Qt WebEngine for Graph view
- SQLite
- CMake

## Project Layout

```text
OpenTree/                     <- repository root (this folder is the working tree)
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ CMakeLists.txt
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ build_msvc.bat            <- MSVC 2022 + Qt WebEngine build
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ build_mingw.bat           <- MinGW build (no WebEngine, graph falls back to text)
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ src/
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ resources/
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ tests/                    <- core test suite (OpenTreeTests)
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ assets/
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ docs/
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ installer/
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ third_party/
ÃƒÂ¢Ã¢â‚¬ÂÃ¢â‚¬Å¡   ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ include/
ÃƒÂ¢Ã¢â‚¬ÂÃ¢â‚¬Å¡   ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ dll/
ÃƒÂ¢Ã¢â‚¬ÂÃ¢â‚¬Å¡   ÃƒÂ¢Ã¢â‚¬ÂÃ¢â‚¬ÂÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ lib/
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ CHANGELOG.md
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ README.md
ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ TIMELINE_PLAN.md
ÃƒÂ¢Ã¢â‚¬ÂÃ¢â‚¬ÂÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ PROJECT_STATUS.md
```

`github/` is the legacy export bundle from before the repository root became the working
tree. It is ignored by git and kept only for reference.

## Requirements

### Build requirements

- Windows 10 or later
- Visual Studio 2022 Build Tools or full Visual Studio 2022
- Qt 6.8.x MSVC 2022 64-bit
- CMake 3.24+

### Qt modules

At minimum:

- `Qt6 Widgets`
- `Qt6 Sql`
- `Qt6 Concurrent`

Optional but recommended for full Graph view:

- `Qt6 WebEngineWidgets`
- `Qt6 WebChannel`

No other modules are needed. Note that Qt PrintSupport is **not** required by OpenTree's own
code (the PDF report uses `QPdfWriter` from QtGui and there is no `QPrinter` usage), but the
WebEngine build does need it at runtime because `Qt6WebEngineWidgets.dll` imports it, so
`windeployqt` keeps deploying it there.

## Build Instructions

### Fast path

Edit `build_msvc.bat` if your Qt path is different, then run:

```bat
build_msvc.bat
```

That script:

1. loads the VS 2022 developer environment
2. configures CMake into `build-msvc/`
3. builds the app
4. runs `windeployqt` through CMake post-build deployment

Always check a fresh build by launching the produced exe with only the system directories on
`PATH` (`set "PATH=C:\Windows\system32;C:\Windows"`): that is what double-clicking from
Explorer does, and it catches missing deployed DLLs that a developer shell hides.

Expected output:

```text
build-msvc/OpenTree.exe
```

### Manual CMake build

```bat
cmake -S . -B build-msvc -G "NMake Makefiles" -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build-msvc --config Release
```

## Running

Run:

```bat
build-msvc\OpenTree.exe
```

Background snapshot mode:

```bat
build-msvc\OpenTree.exe --background-snapshot
```

### Diagnostics and headless modes

These are handy for support, scripting and verification (none of them need a visible window):

```bat
OpenTree.exe --test-scan <path>                 :: compare Everything output against a filesystem walk
OpenTree.exe --find-duplicates <path> [minMB] [all]
OpenTree.exe --test-trash <path>                :: stage a path in the virtual trash (database only, never deletes)
OpenTree.exe --export-report <scanPath> <out.html|out.pdf>
OpenTree.exe --insights <path> [staleDays]      :: disk forecast, stale files and junk candidates
OpenTree.exe --test-ledger <path>               :: verify the Merkle ledger and three-tier routing
OpenTree.exe --render-chart-preview <out.png> [pie|bars|treemap[N]|pie+freespace]
OpenTree.exe --render-details-preview <out.png> [flat]
OpenTree.exe --render-window-preview <out.png> [path] [tabIndex]
OpenTree.exe --smoke-graph <path>                 :: checks the lazy WebEngine start (the graph needs a real window)
OpenTree.exe --smoke-subfolder <root> <child>    :: checks that a child folder reuses the scanned root
OpenTree.exe --dump-graph-html <path> <out.html> :: writes the graph page for inspection in a browser
```

### Tests

```bat
build-msvc\OpenTreeTests.exe      :: or: ctest --test-dir build-msvc -R core
```

## Installer

This repo includes Inno Setup scaffolding for a Windows installer.

### Requirements

- Inno Setup 6 installed

### Build installer

1. build the app first
2. make sure `build-msvc\OpenTree.exe` exists and the deployed runtime is present
3. run:

```bat
installer\build_installer.bat
```

The installer script packages the deployed contents from `build-msvc/`.

The installer currently produces a normal Windows setup flow with:

- install directory selection
- Start Menu shortcut
- optional desktop shortcut
- app icon
- post-install launch option

Planned release quality improvements:

- test install/uninstall on a clean machine
- add versioned release artifacts
- add changelog/release notes per version
- optionally add code signing later if distributed broadly

## Themes

Built-in themes:

- OpenTree Dark
- Light

External themes can be added under the config themes directory and reloaded from the app menu.

Expected shape:

```text
themes/
ÃƒÂ¢Ã¢â‚¬ÂÃ¢â‚¬ÂÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ my-theme/
    ÃƒÂ¢Ã¢â‚¬ÂÃ…â€œÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ theme.json
    ÃƒÂ¢Ã¢â‚¬ÂÃ¢â‚¬ÂÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ theme.qss
```

## Notes

- Graph view depends on Qt WebEngine. Chromium is only started when the Graph tab is first
  opened, and the view is released again when it is left, so the graph costs nothing until
  it is actually used.
- The app currently targets Windows-first workflows.
- The repo includes the Everything SDK headers/imports/binaries. The Everything index is the
  preferred scan engine and the filesystem walk is the automatic fallback when Everything is
  not installed or not running (see Tools > Use Everything Index).
- The scan/cache handoff stays centered on `ScanResult`, so all views work identically with
  either engine.
- The virtual trash stages deletion intents in the database. It only touches files when the
  user explicitly confirms "Move to Recycle Bin", and it refuses drive roots and protected
  system locations.

## Known Limitations

- The deployed Qt tree may warn about missing `Qt6SerialPort.dll` for an optional Positioning plugin during deployment.
- Multi-root tree browsing is supported, but right-side panels still follow one active root context at a time.
- Graph view remains dependent on Qt WebEngine for full functionality.
- Cached reload currently prioritizes fast folder-tree display; the background refresh still does a full filesystem scan.
- App icon may not render on titlebar in some Windows configurations ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â shell cache or `.ico` format compatibility.

## Known Bugs / Rough Edges

- The graph deployment step may emit non-blocking optional plugin warnings on some Qt installs.
- Timeline can still leave the global details pane visible in some pre-scan activation cases.
- Installer/output polish is present, but packaging should still be validated on a clean machine before release.
- App icon may not appear on titlebar in some Windows configurations.
- Some graph layouts on very large folder sets (>500 nodes) may need longer stabilization.

## Roadmap

Short-term:

- Address bar autocomplete using scanned paths
- App icon fix for Windows titlebar
- More packaging polish and versioned release assets

Medium-term:

- Improved multi-root workflow polish across all panels
- Scheduled/automatic duplicate and junk sweeps built on the virtual trash
- Improved theme packaging and community theme docs

Long-term:

- Fully production-ready installer/release pipeline
- Broader Windows integration polish
- Optional deeper search/index integration paths

Completed in 0.6.0:

- Duplicate detection (`Ctrl+7`)
- Insights: disk-full forecast, stale files, junk candidates (`Ctrl+9`)
- HTML/PDF report export
- Virtual trash with staged deletion (`Ctrl+8`)
- Unified settings dialog and tray residency
- Merkle-style structural ledger with per-folder history and three-tier routing
- Core test suite
- Git repo rooted at the working tree, with the release docs/CI/installer alongside the sources

## License

MIT. See `LICENSE`.
