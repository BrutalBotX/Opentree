# Changelog

All notable changes to this project should be documented in this file.

The format is loosely based on Keep a Changelog.

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
