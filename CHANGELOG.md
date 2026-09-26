# Changelog

All notable changes to this project should be documented in this file.

The format is loosely based on Keep a Changelog.

## [0.6.0] - 2026-09-27

### Added

- Report export: File > "Export Report as HTML..." and "Export Report as PDF...". The
  report contains the root summary, largest folders, largest files and a file-type
  breakdown. HTML always works; PDF is compiled in when Qt PrintSupport is present.
- Headless report check: `OpenTree.exe --export-report <scanPath> <out.html|out.pdf>`.
- Virtual trash: a Trash tab that stages deletion intents (database only, nothing on disk
  is touched), shows the projected reclaim, and can send the staged items to the Windows
  Recycle Bin behind an explicit confirmation. Drive roots and protected system locations
  are refused, and the destructive action is styled and defaults to Cancel.
- Schema groundwork for the remaining features: `virtual_trash`, `master_folders`,
  `snapshot_ledger` and `resolution_rules` tables.
- `--test-trash <path>` headless check of the staging bookkeeping (never deletes anything).

### Changed

- Version bumped to 0.6.0.

## [0.5.0] - 2026-09-27

### Added

- Duplicates tab with a staged duplicate finder (size grouping, then a partial hash, then a
  full hash for confirmed candidates), a minimum-size filter, and grouped results showing
  the reclaimable space. `Ctrl+7`.
- Duplicates "Skip system folders" toggle (on by default): Windows, Program Files,
  ProgramData, WinSxS, packaged app caches, the recycle bin, volume metadata and
  page/hibernation files are left out of the hash scan, since they are slow to read and
  can hit locked system files.
- Headless duplicate check: `OpenTree.exe --find-duplicates <path> [minSizeMB] [all]`.

### Fixed

- Expand All no longer locks up or crashes large trees: the warning/confirmation dialog
  explains the risk and defaults to Cancel, expanding more than 5,000 folders is refused
  outright (so a whole drive root cannot be expanded by accident), tree-visibility changes
  are coalesced into a single graph update instead of one per expanded node, and the
  graph's visible-path filter is now an O(1) set lookup instead of an O(n) list scan per
  entry.
- Details table % of parent reserves its own space at the right of the cell, so the bar can
  never run underneath the number.
- Check boxes are themed (accent-filled indicator with a check icon) instead of falling back
  to the default indicator colors.
- Combo boxes and spin boxes reserve room for their drop-down/arrow controls, so the text
  no longer runs under the arrow ("By extension", the duplicates size selector), and the
  Extensions grouping combo has a sensible minimum width.
- Graph HTML is emitted as concatenated string literals so the MSVC build no longer fails
  with C2026 (string too big).

### Changed

- Graph: the hierarchical Tree layout is now the default for graphs with more than ten
  nodes (previously 30), the root node is pinned at the origin in Force mode so children
  radiate instead of drifting into a hairball, and the force/tree spacing was tuned.
- Treemap now honours "Include free space" (a free-space tile at drive roots).
- Version bumped to 0.5.0.

## [0.4.0] - 2026-09-26

### Added

- Everything SDK scan engine is now the preferred scan path, with automatic filesystem
  fallback and a clear status message about which engine ran.
- Tools menu entries: "Use Everything when available" (checkable), "Locate Everything
  Executable...", and "Test Everything Connection".
- New `Details` tab: sortable per-item table with size, % of parent (inline bar), file and
  folder counts, and modified time, plus an "Include subfolders" flat mode.
- New toolbar drive selector showing the current volume's free/used bar with a drop-down
  of all ready drives.
- Graph view toolbar: Force/Tree layout toggle, Fit, zoom in/out and Re-layout. Layout
  defaults to the hierarchical Tree view for larger graphs.
- Graph label budget: only the largest nodes keep a permanent label, the rest reveal their
  name on hover.
- Extensions view can group files by extension or by file family (Video, Audio, Image,
  Document, Archive, Disk image, Code/Text, Program, Font, Other).
- Chart panel can include the volume's free space as its own slice when scanning a drive
  root.
- Headless diagnostics: `--test-scan <path>`, `--render-chart-preview <out.png>
  [pie|bars|treemap]`, `--render-details-preview <out.png>`,
  `--export-details-csv <out.csv>` and
  `--render-window-preview <out.png> [path] [tabIndex]`.
- Graph view "Follow tree expansion" checkbox: on by default, so the graph matches the
  folders expanded in the tree.
- Treemap depth selector inside the Treemap subtab (Depth 1/2/3).
- Graph "Hover focus": hovering a node zooms/focuses it and restores the previous view
  when the pointer leaves; toggleable from the graph toolbar.
- Details table CSV export (File menu or the panel's Export CSV button).
- `build_mingw.bat` helper for building without Visual Studio, and `build_msvc.bat` now
  locates Visual Studio via `vswhere`.

### Fixed

- Everything availability is verified over IPC instead of only checking that the DLL
  loaded, so a stopped service is reported correctly.
- Everything queries use `SetMatchPath` with a quoted, non-regex path instead of the
  invalid `parent:` regex, so scans actually return results.
- Large Everything results are retrieved in pages instead of being silently truncated.
- Graph planet SVGs emit valid percentages (the previous `%%` produced invalid SVG
  gradients because `QString::arg` does not unescape).
- Single-clicking a graph node no longer switches to the Timeline tab.
- Graph payload keeps the selected folder's ancestor branch visible again.
- Direct file diamond nodes and the `Other files` aggregate node are now emitted instead
  of being collected and discarded.
- Removed the runtime JS string-patching hack that silently broke when the page JS
  changed; the starfield is redrawn per frame so it no longer accumulates motion-trail
  streaks, and it is DPI aware.
- The graph now fits the whole graph on render instead of zooming onto the selected node.
- Pie labels no longer overlap the pie or each other, long names elide, and every slice
  gets a label when there is room; the gutter is sized from the measured label widths so
  the pie is as large as possible.
- Bar chart no longer silently clips rows and elides long labels/values.
- Timeline mode now truly hides the global details pane; tab mode is detected by widget
  instead of tab index.
- MSVC deployment no longer warns about the optional Positioning plugin.
- The graph node budget is now read from the `Graph/MaxNodes` setting.

### Changed

- Filesystem scanning is no longer the only scan path; Everything is attempted first when
  enabled.
- Treemap rewritten as a squarified layout; labels are clipped and elided to their tile and
  the text color adapts to the tile for contrast.
- Bars are sorted by the active metric instead of pie order.
- Details table percent column: the value is right aligned and switches to white when the
  bar runs under it; header alignment is consistent; column auto-sizing is skipped on very
  large folders.
- Heatmap now uses a real heat scale across the whole row (blue by share, red/green for
  snapshot deltas).
- Graph now defaults to following the tree's expansion ("Follow tree expansion" on) and
  clamps its fit zoom so nodes stay readable; the summary shows the other-folder cutoff.
- Details panel no longer reports a bogus "Allocated"/"Compression Rate" for folders (the
  directory entry allocation was being compared against the whole subtree size).
- Selecting an item that belongs to a different scanned root now switches the active root
  first; previously the Extensions/Chart/Heatmap/Details views stayed scoped to the old
  root and could appear empty.
- Heatmap now lists only the folders directly inside the selected folder, with % of parent.
- Details table % of parent uses the item's real immediate parent, so flat mode is correct.
- Theme now styles `QProgressBar`, `QScrollBar` and `QTableView`; previously the progress
  bar and scrollbars fell back to the default light widgets in dark mode.
- Extensions view falls back to file entries in the tree data and clearly reports when a
  folder has no files in the current scan.
- Details table % of Parent no longer draws the value twice; the delegate now paints only
  the background itself instead of letting the base class re-draw the text under the bar.
- "Include subfolders" (flat mode) no longer empties out for drive roots: descendant
  matching is now shared via `PathUtils::isSameOrDescendant`, which handles drive roots
  (`C:/`) and trailing separators. The same fix covers the Extensions view on `C:`.
- Pie "Include free space" now uses one denominator for every slice, so the free-space
  slice no longer overlaps the used slices.
- Graph mouse input: single click selects immediately (the artificial 250 ms click delay is
  gone), clicks that end a pan are ignored, right-click always suppresses the browser menu,
  and background clicks clear the selection.
- Graph selection recolours in place instead of rebuilding the page, so the camera and
  layout no longer jump on every click.
- Graph hover focus now waits for hover intent, moves the camera less aggressively, and is
  cancelled on press/drag.
- Graph hover zoom no longer fights the user: a hover caused by the camera sliding a node
  under a stationary pointer is ignored (no zoom-in/zoom-out feedback loop), and leaving a
  node no longer zooms back out automatically. Press Escape or use Fit to go back.
- Header sort indicators and combo arrows are now styled with icon images; previously they
  rendered as a stray blob or were missing.
- Extensions, Heatmap and Details tables use explicit column widths so the header text is
  not clipped by the sort-indicator padding.
- Clear All Roots now asks for confirmation.
- About dialog shows the version, Qt version and build date.

## [0.3.1] - 2026-07-10

### Fixed

- Embedded schema resource alias now matches the database bootstrap path, so installed builds can initialize the database without falling back to a missing external file.

### Changed

- GitHub export docs and installer/build versioning were bumped to keep the packaged release metadata in sync with the current build.

## [0.3.0] - 2026-07-10

### Added

- Snapshot manager dialog with a real tabulated table view for snapshots.
- Snapshot manager delete flow that removes snapshots and their related snapshot items/file events.
- Graph-to-Timeline selection wiring so graph node activation can jump to Timeline snapshot context.
- Root-scoped cached scan reload path: previously scanned roots can show cached folder data before the fresh scan completes.
- Database migration for cached file rows to store `root_path`.

### Changed

- Timeline snapshot ordering now renders oldest to newest for list/trend flow.
- Timeline compare state now resets more aggressively on scan and clear-root transitions to avoid stale compare output.
- Cached reload now prefers fast folder-summary display instead of loading every cached file row before showing UI.
- Folder/file cache persistence is now root-scoped instead of deleting all cached scan rows globally.
- Shared theme coverage/readability was extended across menus, dialogs, tables, inputs, and buttons.

### Fixed

- Snapshot timestamps now render with a space-separated date/time instead of raw ISO `T` formatting.
- Dead Timeline compare plumbing in the normal details panel path was removed.
- Graph click plumbing now routes through the main window/controller instead of stopping inside the graph widget.
- Cache root lookup now normalizes root keys consistently so repeat scans can match stored root data.

### Known Issues

- Timeline can still leave the global details pane visible in some pre-scan activation cases.
- Cached reload is faster than a cold scan, but a full background filesystem scan still runs after the cached tree is shown.
- Deployed Qt may emit non-blocking warning about missing `Qt6SerialPort.dll` for optional NMEA positioning plugin.

## [0.2.0] - 2026-07-06

### Added

- Graph right-click context menu: "View in Pie Chart", "View in Bar Chart", "View in Treemap", "View in Extensions", "View in Heatmap". Right-clicking a graph node navigates to the target tab with the item selected.
- Pie chart surround labels with color-matched leader lines. Labels are evenly distributed vertically on each side of the pie (no rejection).
- Galaxy particle background in Graph view: 300 particles (90% static specks, 10% drifting, 12 pulsing stars) with independent random-walk motion and wrap-around.
- Inline SVG planet node icons assigned by folder size percentile: 5 distinct styles (gray cratered moon, red rocky, green Earth-like, cyan striped gas giant, gold ringed gas giant).
- Graph-to-tree sync: double-clicking a folder in the graph now expands it in the left tree panel, showing children automatically.
- `ChartPanel::setActiveViewMode(ViewMode)` public method for programmatic chart sub-tab switching.
- `MainWindow::navigateToEntryRequested` signal for cross-tab navigation from graph context menu.
- `GraphPanel::viewInTabRequested` signal emitted when user picks a tab from the graph context menu.

### Changed

- Complete graph visual overhaul: neon color palette (`#00D4FF` cyan, `#FF4081` pink, `#39FF14` lime, `#FFD700` gold, `#B388FF` purple). Dramatic forceAtlas2Based physics (`gravitationalConstant: -22`, `springLength: 220`) with sustained subtle drift after stabilization.
- Graph node shapes replaced with procedurally generated SVG planets (`shape: 'circularImage'`). State indication moved to border colors and widths.
- Pie chart labels moved from legend-column to surround-style with evenly distributed vertical placement. Leader lines are single straight lines from slice edge midpoint to label. No more greedy rejection loop.
- Graph background star particles changed from collective orbital motion to independent straight-line drift (90% static, 10% moving).
- Chart canvas size reduced slightly to give more room for surround labels.
- Removed "Show in Explorer" from all chart context menus (unreliable with non-filesystem virtual paths).
- treemap: threshold filter applies at all depths including root (no more stray tiny rectangles).
- treemap: 300px² minimum rectangle area prevents unreadably small nested boxes.
- treemap layout now persists into member variables instead of discarded local copies — fixes hit-testing for hover/click/context-menu.
- treemap: nested layout properly recurses into child nodes with alternating slice-and-dice orientation.
- Simplified `GraphBridge` — uses minimal `QObject` with `std::function` callbacks, no full `GraphPanel` exposure to WebChannel.
- Removed `QFileSystemModel` from chart/graph panels to prevent startup crash (`std::bad_alloc` from empty root path).

### Fixed

- vis.js `nodes.size` error: removed invalid `{ min, max }` object — vis.js expects a single number.
- Pie chart label overlap and clipping: replaced greedy rejection with even vertical distribution on each side of the pie.
- Pie chart leader lines now correctly originate from the slice's midpoint on the pie edge, not from an incorrect vertical projection.
- Double-click in graph no longer double-navigates: 250ms timer guard on click events — double-click cancels the pending click and only fires `openNode`.
- Collective particle orbit motion (all stars rotating together as a rigid disk) replaced with independent random-walk drift.
- Particles no longer converge to center of window (removed `(cx - p.x) * 0.00004` spring term).
- treemap hit-testing: `paintTreemap` now lays out `m_treemapNodes` directly (was discarding layout into a local copy).
- treemap root-level threshold now applies to depth-0 children (removed `depth > 0` guard).
- `MainWindow::ensureGraphPanel` now correctly wires `viewInTabRequested` signal on creation.
- `ChartSlice.pieLabelRect` and `pieLabelVisible` properly set per-slice with surround label positions.
- Hover/click detection works on pie surround labels via `sliceAt()` → `pieLabelRect` check.
- Chart context menus for treemap also removed Show in Explorer (was only removed from pie/bar branch initially).

### Known Issues

- App icon still not appearing on titlebar in some Windows configurations despite `.rc` and `setWindowIcon` calls — likely `.ico` format compatibility or shell cache.
- Treemap right-click and hover may still have minor hit-testing offsets when deeply zoomed.
- Deployed Qt may emit non-blocking warning about missing `Qt6SerialPort.dll` for optional NMEA positioning plugin.
- Some graph layouts on very large folder sets (>500 nodes) may need longer stabilization.

## [0.1.0] - 2026-07-01

### Added

- Qt 6 desktop application foundation
- Multi-root tree browsing in one session
- Graph view with WebEngine path and local `vis-network` bundle
- Chart workspace with Pie, Bars, and Treemap tabs
- Heatmap, Extensions, and Timeline panels
- Snapshot creation, comparison, and background snapshot mode
- Theme system with built-in light/dark themes and external theme reloading
- Inno Setup installer scaffold

### Changed

- Address-bar based navigation added to chart/graph workspaces
- Pie interaction simplified to legend-based interaction to avoid misleading hit regions

### Known Issues

- Some graph/chart interactions still need refinement on dense datasets
- Treemap nested subfolder depth is deferred until recursive subdivision is implemented properly
- Deployment may warn about optional missing `Qt6SerialPort.dll` in some Qt installations
