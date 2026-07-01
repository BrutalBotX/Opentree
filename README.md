# OpenTree

OpenTree is a Qt 6 desktop disk-usage explorer for Windows focused on large-folder analysis, timeline snapshots, and multiple visual views of storage usage.

It is designed as a Windows-first visual storage analysis tool with a TreeSize-like workflow, but with built-in timeline/snapshot analysis and richer experimental views such as graph, heatmap, chart workspace, and multi-root navigation.

## Current Features

- Multi-root disk tree browsing in one app session
- Explorer-style path/address entry for graph and chart workspaces
- Details pane for files and folders
- Graph view for folder structure and top direct files
- Chart workspace:
  - Pie
  - Bars
  - Treemap
- Heatmap view
- Extensions view
- Timeline and snapshot comparison
- Background snapshot mode
- Theme support with built-in light/dark themes and reloadable external themes
- Windows installer scaffold with Inno Setup

## Tech Stack

- C++17
- Qt 6 Widgets
- Qt WebEngine for Graph view
- SQLite
- CMake

## Project Layout

```text
OpenTree/
├── CMakeLists.txt
├── build_msvc.bat
├── src/
├── resources/
├── third_party/
│   ├── include/
│   ├── dll/
│   └── lib/
├── installer/
│   ├── OpenTree.iss
│   └── build_installer.bat
├── CHANGELOG.md
├── CONTRIBUTING.md
└── docs/
    └── PROJECT_STATUS.md
```

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
└── my-theme/
    ├── theme.json
    └── theme.qss
```

## Notes

- Graph view depends on Qt WebEngine.
- The app currently targets Windows-first workflows.
- The repo includes Everything SDK headers/imports/binaries used by the project, but filesystem scanning remains the active default path.

## Known Limitations

- The deployed Qt tree may warn about missing `Qt6SerialPort.dll` for an optional Positioning plugin during deployment.
- Some chart/graph UX is still under active refinement.
- Treemap nested subfolder depth is intentionally deferred until recursive box subdivision is implemented properly.
- Pie chart interaction now relies on legend-based hover/click to avoid misleading slice hit areas.
- Multi-root tree browsing is supported, but right-side panels still follow one active root context at a time.
- Graph view remains dependent on Qt WebEngine for full functionality.

## Known Bugs / Rough Edges

- Some graph/chart interactions are still being refined for large or visually dense folders.
- The graph deployment step may emit non-blocking optional plugin warnings on some Qt installs.
- Installer/output polish is present, but packaging should still be validated on a clean machine before release.
- Pie mode currently uses legend-based interaction instead of direct slice hit interaction to avoid misleading slice hover/click behavior.
- Treemap nested depth control is intentionally deferred until recursive subdivision is implemented properly.

## Roadmap

Short-term:

- Proper recursive treemap depth rendering
- Better graph/treemap visual polish for dense datasets
- Cleaner chart legend/data presentation
- More packaging polish and release assets

Medium-term:

- Better report/export capabilities
- More advanced snapshot analytics
- Improved theme packaging and community theme docs
- Better multi-root workflow polish across all panels
- Real recursive treemap depth support

Long-term:

- Fully production-ready installer/release pipeline
- Broader Windows integration polish
- Optional deeper search/index integration paths

## License

MIT. See `LICENSE`.
