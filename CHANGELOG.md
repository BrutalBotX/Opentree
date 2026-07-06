# Changelog

All notable changes to this project should be documented in this file.

The format is loosely based on Keep a Changelog.

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
