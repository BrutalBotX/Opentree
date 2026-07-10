# Timeline Plan

## Goal
- Make the Timeline tab compact, functional, and readable.
- Replace the normal details pane with Timeline-specific snapshot info only when Timeline is active.

## Fix Split First
- When Timeline is active, the normal file/folder details view should not appear at all.
- Replace the right pane with a Timeline-specific compare/details view for that tab only.
- Leaving Timeline should restore the normal details panel exactly as before.
- Verify the details pane is fully hidden in Timeline mode, not just visually dimmed.
- Verify Timeline selection does not trigger compare automatically.
- Make Timeline self-contained with its own left/main area and right compare/details pane.
- Do not reuse the global app details pane for Timeline.

## State Rules
- Rescan and clear-roots reset Timeline compare state.
- Switching tabs restores the previous non-Timeline details panel behavior.
- Tree and graph selections outside Timeline still update the normal details panel.
- No stale compare info should leak into other tabs.
- No stale file metadata should leak into Timeline mode.

## Reduce Clutter
- Keep only:
  - current-root summary
  - scheduled snapshot diagnostics
  - trend graph
  - snapshot list
  - compare action
- Remove or hide by default:
  - redundant text blocks
  - duplicate compare summaries
  - always-expanded raw tables
- Make compare output compact-first:
  - summary header
  - top movers
  - event summary
  - optional expandable detail lists later
- Keep the 2-level delta view compact:
  - level 1: compare summary
  - level 2: per-folder row deltas / top movers / file events
- Keep the level 1 compare view to just:
  - snapshot compared
  - size change
  - folders changed
  - files/events changed
- Replace raw delta-byte text with formatted size text.

## Rebuild Timeline Flow
- Main Timeline area:
  - Overview card
  - Trend graph
  - Snapshot list
- Right Timeline pane:
  - compare summary
  - top movers
  - event feed
  - optional row-level folder delta list if space allows
- Timeline itself owns this split; the global app details pane stays for non-Timeline tabs only.

## Snapshot Context Menu
- Keep only:
  - Compare with Current Scan
  - Copy Snapshot Text
- Optional later:
  - Copy Snapshot ID / Date
  - Jump to latest snapshot

## Snapshot Manager
- Add a real `Manage Snapshots...` dialog.
- Show id, date, root, and size/file count.
- Support deleting a selected snapshot.
- Deleting a snapshot must remove its related items and file events.
- Confirm deletion before executing it.

## Compare Rendering
- Default compare view:
  - snapshot compared
  - total delta
  - changed folder count
  - file event count
  - biggest growth/shrink
- Then show:
  - top movers list
  - file event list
- Put full folder delta rows behind:
  - a compact list
  - or an expandable section
- Use colored text for growth/shrink direction and keep exact byte values out of the primary summary.

## Graph Highlight
- Clicking a graph node should select the matching snapshot in Timeline.
- Treat the clicked graph path as a root/path match, not just an exact snapshot label.
- Opening a graph node should still preserve the normal graph open behavior.

## Graph Order
- Older snapshots should render on the left.
- Newer snapshots should render on the right.
- If the current graph order is reversed, flip only the timeline ordering logic.

## Size Units
- Add an app-wide size display mode.
- Default to adaptive units:
  - `>= 1 GB` -> GB
  - `>= 1 MB` -> MB
  - `>= 1 KB` -> KB
  - else bytes
- Treat this as formatting policy, not a `ViewMetric` change.
- Store it separately from size/percentage/file-count view mode.
- Centralize formatting in `SizeFormatter` and reuse it everywhere the app prints sizes.
- Add a View menu entry or submenu for the size-unit setting.

## Theme Coverage
- Keep the built-in theme palette/style readable in dark and light modes.
- Ensure menus, dialogs, buttons, input boxes, combo boxes, time edits, and tables use theme colors.
- Do not reintroduce rounded button styling that hurts contrast.

## Final Verification
- scan folder -> open Timeline
- open Timeline before scan -> scan folder -> no normal details leak into Timeline mode
- create snapshot -> snapshot appears for current root
- open Manage Snapshots -> rows are tabulated and readable
- delete snapshot from manager -> table refreshes cleanly
- select snapshot -> context menu compare works
- compare -> compact compare pane appears on right
- switch away from Timeline -> normal details restored
- clear roots -> Timeline and right pane reset cleanly
- resize window small/large -> no overlap
- confirm implemented now vs intentionally deferred

## Current Execution Focus
- Replace snapshot manager list text with a real table.
- Fix Timeline-active scan flow so the normal details panel does not get repopulated behind Timeline.
- Collapse the global details pane out of the right splitter when Timeline is active; hiding content alone is not enough.
- Reset Timeline compare state on scan/clear-root transitions where stale compare output can persist.
- Flip Timeline ordering to oldest -> newest if still reversed in the list/trend.

## Deferred Bugs
- Timeline can still leave the global details pane visible in some pre-scan activation cases. Leave this marked and return after cache-first scan work.

## Next Feature
- Show cached root data immediately when opening/scanning a root.
- Refresh that root in the background and replace the UI when the fresh scan completes.
- Keep the scan pipeline compatible with a later Everything SDK-backed scan path by preserving `ScanResult` as the handoff type.

## Implementation Order
1. Right-pane swap for Timeline mode
2. Timeline clutter reduction/layout cleanup
3. Compact compare pane
4. Trend graph retention/polish
5. App-wide adaptive size formatting
6. Snapshot context menu validation
7. State/reset cleanup
8. Dead code cleanup
9. Full verification against docs

## Requirements From Docs
- Timeline scoped to current scanned root.
- Snapshot compare against current scan.
- File event logging for ADD, DELETE, MODIFY.
- Lightweight analytics and size trend.
- Scheduled snapshot diagnostics.
- Context-menu driven compare flow.
- Timeline as the single place for snapshot trend/history/compare.

## Deferred By Docs
- Graph-backed historical timeline.
- Per-folder timeline visualization.
- Full Merkle-ledger / lifecycle architecture.
