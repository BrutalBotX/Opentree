# OpenTree Project Status

## Current Phase

Phase: vis.js graph visualization + MSVC Qt WebEngine build enabled

This file tracks what is done, what is intentionally deferred, and what the next phase should focus on.

## Done In This Phase

### App foundation

- Qt 6 desktop application builds and launches successfully
- Matching Qt-compatible MinGW toolchain installed and retained for the legacy build
- Qt 6.8.0 MSVC 2022 + WebEngine toolchain is installed and now builds successfully
- Dedicated MSVC WebEngine build output is generated in `build-msvc/`
- Runtime deployment is handled for Qt DLLs and compiler/runtime dependencies for both MinGW and MSVC builds
- Main window layout is working and resizable with minimum sizes
- A compact top toolbar now exists for core actions like open, rescan, snapshot, compare, explorer, and copy-path

### Layout and navigation

- Explorer-style layout is in place:
  - left tree pane
  - center tab area
  - right details pane
- Tree starts with root expanded and subdirectories collapsed
- Tree header is hidden
- Tree filter box works recursively
- Tree context menu supports:
  - Open
  - Show in Explorer
  - Copy Path

### Scan workflow

- Scans currently use filesystem scanning only
- Scan runs asynchronously
- Status bar progress bar is visible during scans
- Scan results populate:
  - folder structure
  - files in tree
  - details panel
- Drive root normalization bug fixed so full drive roots like `E:\` scan correctly

### Details panel

- Details panel shows:
  - Name
  - Path
  - Size
  - Allocated
  - % of Parent (Size)
  - Files
  - Folders
  - Last Modified
  - Last Accessed
  - Creation Date
  - Owner
  - Attributes
  - Compression Rate
  - Permissions
  - Path Length
- Quick actions in details panel:
  - Open
  - Show in Explorer
  - Copy Path
- Compression rate no longer shows negative values
- Unknown compression/allocation cases fall back to `0%` or `-` where appropriate

### Menus

- File menu:
  - Scan Folder
  - Recent Roots
  - Rescan Current Root
  - Clear All Roots
  - Exit
- Snapshots menu:
  - Create Snapshot
  - Compare Snapshot
  - Snapshot Settings
- Tools menu:
  - Locate Everything SDK
- View menu:
  - View by Size
  - View by Percentage
  - View by File Count
  - Refresh
  - Expand All
  - Collapse All
  - Tabs submenu for Graph / Chart / Extensions / Heatmap / Timeline
  - Set Others Threshold...
  - Theme submenu with built-in and reloadable themes
- Tools menu now includes explorer/terminal/path/config/log convenience actions beyond SDK setup
- Help menu:
  - About OpenTree

### Database

- SQLite database initialization works
- Core scan tables exist:
  - `folders`
  - `files`
- Snapshot tables exist:
  - `snapshots`
  - `snapshot_items`

### Snapshots

- Snapshot creation is now delta-oriented instead of restore-oriented
- First snapshot for a root acts as a baseline snapshot
- Snapshot creation compares against the last snapshot for the same root path
- Only changed folders above the configured threshold are stored
- If threshold is not exceeded:
  - no snapshot is created
  - user gets explicit feedback
- Timeline tab shows snapshots scoped to the current scanned root
- Timeline compare button compares selected snapshot against current scan
- Compare now renders inside the timeline tab instead of a popup
- Timeline now uses a real line-and-dots timeline widget for the current root
- Timeline also shows visible text snapshot entries for the current root
- Snapshot menu compare action is wired to the currently selected snapshot in Timeline
- Snapshot compare includes per-folder delta rows
- Snapshot compare includes file event logging and display for:
  - ADD
  - DELETE
  - MODIFY
- Timeline now shows lightweight analytics using existing snapshot data:
  - latest size summary
  - net growth since first visible snapshot
  - simple total-size trend chart
  - top growers / top shrinkers text summary from compare rows

### Snapshot scheduling groundwork

- Snapshot settings dialog exists
- Configurable snapshot settings include:
  - enabled/disabled
  - cadence: daily / weekly / monthly
  - scheduled time
  - delta threshold
  - retention window
  - whitelist directories
- Task Scheduler integration exists
- Background mode entry exists via:
  - `--background-snapshot`
- Background mode currently:
  - loads config
  - reads whitelist
  - scans whitelisted roots
  - creates thresholded snapshots
  - compacts old file events by retention window
  - records background run summary
  - exits
- Timeline now shows lightweight scheduled snapshot diagnostics using saved config plus latest background run:
  - schedule enabled/disabled
  - cadence and run time
  - whitelist root count
  - latest recorded background run status, roots processed, snapshots created, and compaction count

### Verified functional state

- Database initialization now succeeds reliably
- Snapshot data is stored in AppData SQLite successfully
- Timeline snapshot entries populate for the current root
- Snapshot compare is functional against the current scan
- File-level snapshot events are stored for add/delete/modify changes
- Retention/compaction support exists for old snapshot file events
- Background run logging exists in the database
- Background snapshot mode has been exercised against a test whitelisted root and updates the AppData database
- Timeline selection and compare flow work through the real timeline widget
- MSVC Qt WebEngine configure/build now completes successfully in `build-msvc`
- WebEngine deployment completes, including `QtWebEngineProcess.exe` and required resource packs
- Graph payload escaping was hardened during the MSVC port so quotes, backslashes, and newlines are emitted more safely into the embedded JavaScript payload
- Graph tab click selection now routes into the existing right-side details panel
- Graph tab selection now also syncs the matching item in the left tree when available
- Tree selection now also drives graph selection/highlighting
- Tree, graph node clicks, and graph breadcrumb path clicks now share a single folder-focus flow
- Graph breadcrumb clicks now open that folder in the graph instead of only changing the label/root silently
- Graph node clicks now sync tree/details and open the graph on that folder
- Tree single-click now expands collapsed folders without immediately collapsing expanded ones
- Tree double-click now collapses expanded folders as a quick inverse action
- Fresh scan results now auto-select the root folder so details/chart/graph start from a real focused item
- Tree branch-indicator clicks are now intercepted so left-side single clicks expand collapsed folders without toggling expanded ones shut
- Graph scope now follows the currently expanded/visible folders in the left tree instead of always opening the full scanned hierarchy
- Embedded graph no longer renders a duplicate node-info sidebar inside the HTML view
- Graph node sizing can now be switched between size, file count, and folder count
- A shared view metric now drives related views together:
  - size
  - percentage
  - file count
- Graph size mode now uses a logarithmic scale so large folders are visibly differentiated instead of clustering into nearly identical node sizes
- Graph double-click now drills into that folder, updates selection/details, and re-renders the graph rooted there
- Graph now has an explicit `Root` / `Up` box node for navigating to the parent/root level
- Graph now includes a clickable breadcrumb path above the view for drilling back to ancestor folders directly
- Graph now uses a vendored local `vis-network` bundle from Qt resources instead of fetching it from a CDN at runtime
- Startup regression after the first graph WebChannel bridge attempt was fixed by removing the local bridge QObject and exposing only explicit invokable methods on `GraphPanel`
- MSVC startup stability was improved by deferring heavy right-side widgets until first use instead of constructing them all during main window startup
- Center analysis workspace now includes additional TreeSize-style tabs:
  - `Chart`: a chart workspace with `Pie`, `Bars`, and `Treemap` subtabs for the active folder's child folders/files
  - `Extensions`: file extension grouping for the active folder subtree
- App theme support now exists with built-in themes and external theme folder loading
- Graph and Chart now include explorer-style editable address bars that drive the same left-tree/controller focus path

## Deferred / Left For Later

### Everything integration

- Everything SDK integration is intentionally not active right now
- Filesystem scanning is the active scan path
- Everything-specific work should be revisited later as a separate phase

### Snapshot UX / analysis

- Compare view exists in the timeline tab with row-level folder deltas
- No graph-backed historical timeline yet
- No per-folder timeline visualization yet

### Snapshot scheduling polish

- Task Scheduler task registration exists, but has not been deeply validated across all cadence modes on real machines
- Timeline diagnostics show saved schedule config and latest background run, but do not inspect the live Windows scheduled task state yet
- No tray/background residency model

### Settings/UI polish

- No unified application settings dialog yet beyond snapshot settings
- No theme management beyond current defaults
- No keyboard shortcut pass

### Reports / cleanup / dedupe

- No report export yet
- No virtual trash flow yet
- No duplicate finder yet
- No cleanup policies yet

### Graph / heatmap / timeline visuals

- Graph tab now renders a real structural graph using vis-network inside `QWebEngineView` with delta coloring
- Graph tab uses the main app details panel for selection instead of a duplicate embedded sidebar
- Graph visibility is now pruned to the folders currently visible/expanded in the tree pane
- Graph sizing modes now support bytes, file count, and folder count
- Graph selection now uses color states for selected nodes and ancestor chain highlighting
- Graph includes a small in-view legend and root/up navigation affordance
- Graph double-click supports folder drill-in with the built-in vis focus animation before re-rooting the graph
- Graph breadcrumb and graph node interactions now both re-root the graph to the chosen folder and sync the rest of the UI
- Chart tab now provides a subtabbed chart workspace with pie, bar, and treemap views based on active-folder children/files
- Chart now includes top direct files alongside top child folders, with separate `Other folders` and `Other files` buckets
- Extensions tab now provides extension grouping for the active folder subtree
- Heatmap tab now renders a size/delta-oriented table-based heatmap view
- Timeline tab remains the single place for snapshot trend/history/compare
- Tree click behavior now prefers expand-once navigation instead of click-to-toggle collapse churn
- Graph now includes top direct files as diamond nodes plus an `Other files` aggregate node under the current folder
- Heatmap now defaults to `%` descending
- Extensions now defaults to `%` descending
- Chart pie slices now render arrow/callout labels with extra in-tab margin to avoid clipping
- Chart single-click now behaves like selecting the same item from the left tree, while right-click adds an `Open in Graph View` action
- Shared metric mode now propagates through Graph, Chart, Extensions, and Heatmap
- Built-in themes now include `OpenTree Dark` and `Light`
- External themes can be added under the app config `themes/` directory and reloaded from the menu

### Deferred chart work

- Treemap subfolder depth control was intentionally removed from the UI because only the menu shell existed; recursive nested treemap rendering still needs a real implementation before it should be exposed again.
- When revisiting treemap depth later, implement:
  - nested subfolder box subdivision up to a selected depth
  - per-depth hit testing and labels
  - consistent interaction with current metric mode
- Pie charts now show tooltip details on hover including name, size/files, and percentage
- Treemap tiles now show richer text including percentage where space allows
- File selection from chart/graph now reselects the file in the left tree after parent focus sync
- Graph path label has been removed in favor of the address bar
- Pie interaction is now legend-row based rather than slice-surface based, to avoid misleading hover/click regions
- Pie uses a color-matched legend for labels instead of leader-line callouts
- Multi-root session switching now reacts to selecting older scanned roots/items so the right-side tabs follow the owning root again
- Dark theme now styles address bars explicitly for readability
- Graph now uses a minimal WebChannel bridge object instead of exposing the full `GraphPanel` QObject

### TreeSize-inspired features worth implementing

- Multi-view analysis workspace should continue toward four core views:
  - chart view for folder share breakdown
  - history view for size vs allocated trend over time
  - extensions/category view for grouping by file type
  - details table view for sortable per-item inspection
- Drive selector should show total capacity plus free/used visual bars for each root
- Chart view should support:
  - click/hover folder highlighting
  - clear size and percent labels
  - optional free-space slice toggle
  - optional 2D/3D or alternate chart presentation only if the simpler chart stops being enough
- Timeline/history view should support:
  - actual size vs allocated size trend lines
  - timestamped snapshot history for the current root
  - zoom/reset controls only if the chart becomes dense enough to need them
- Extensions/category view should support:
  - grouping by extension/type family
  - size, allocated size, percent of parent, and file count columns
  - a treemap for category/file-type share
  - sort by size/name and hide-below-threshold filters
- Details view should continue toward a fuller sortable table with:
  - size and allocated size
  - file/folder counts
  - percent of parent with inline bar
  - modified/accessed dates
  - owner
- Useful file-system actions worth considering later:
  - open in File Explorer
  - open terminal here

## Known Limitations

- Timeline only shows snapshots for the current scanned root; if none exist for that root, it will be empty
- Snapshot storage is thresholded changed-folder storage, not a full reconstruction model
- Background snapshot execution is intentionally minimal and should be hardened later
- Filesystem scan progress is approximate during discovery because total file count is unknown up front
- Graph view depends on Qt WebEngine deployment, but the graph library itself is now bundled locally via Qt resources
- Graph drill-in now has both an in-view up/root box and a clickable breadcrumb path, but no richer back/forward history stack
- Graph only shows folders that are currently visible in the tree, so deep collapsed branches are intentionally omitted until expanded
- Graph, heatmap, timeline, and details creation are deferred so startup stays on the cheap path until each pane is actually used
- MSVC deployment currently emits a non-blocking warning for the optional Qt Positioning NMEA plugin because `Qt6SerialPort.dll` is not installed in the MSVC Qt tree

## Recommended Next Phase

Phase: chart/extensions refinement + packaging cleanup

Recommended order:

1. Refine the new chart views where pie/bar behavior still needs polish
2. Optionally install `qtserialport` for the MSVC Qt tree to eliminate the non-blocking WebEngine/Positioning deployment warning
3. Revisit crash/stability work if the MSVC path still proves flaky in real use
4. Validate scheduled task behavior on real cadence runs if needed

## Practical Backlog

Implement next when useful:

1. A true details-table tab/view for sortable per-item inspection
2. Better timeline/history charting for size vs allocated space
3. Drive selector with free/used bars
4. Optional chart toggles like free-space inclusion and alternate chart modes
5. Richer category families/filters on top of the new extensions view

Explicitly skipped for now:

- email/send/share flows
- compress/decompress actions
- change owner / heavy admin permission tools
- speculative ribbon bloat that duplicates existing controls

## Build / Run Notes

- Legacy MinGW build output: `build/OpenTree.exe`
- MSVC Qt WebEngine build output: `build-msvc/OpenTree.exe`
- Current WebEngine-capable compiler/toolchain: Visual Studio 2022 MSVC + Qt 6.8.0 `msvc2022_64`
- MSVC rebuild helper: `build_msvc.bat`
- Filesystem scanning is the current default path
- Background snapshot mode:
  - `OpenTree.exe --background-snapshot`
