---

# 1) Full Architectural Diagram

```text
┌──────────────────────────────────────────────────────────────────────┐
│                           Desktop Application                        │
│                       Qt 6 + C++17 (recommended)                    │
└──────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────── UI LAYER ────────────────────────────────┐
│                                                                         │
│  MainWindow                                                             │
│  ├── Tree View Panel (virtualized folder tree)                           │
│  ├── Graph View Panel (node-link visualization)                         │
│  ├── Heatmap Panel (treemap / sunburst)                                  │
│  ├── Timeline Panel (snapshot scrubber / wayback machine)               │
│  ├── Details Panel (selected node stats, metadata, actions)             │
│  ├── Reports Panel (export PDF / CSV / HTML)                            │
│  └── Settings / Policies Dialog                                         │
│                                                                         │
│  Shared UI Behaviors:                                                   │
│  - Search/filter                                                       │
│  - Context menus                                                       │
│  - Split panes                                                         │
│  - Async progress indicators                                           │
│  - Snapshot compare toggle                                             │
└─────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────── APPLICATION / DOMAIN LAYER ─────────────────────────────┐
│                                                                                      │
│  AppController                                                                      │
│  ├── routes UI actions to services                                                  │
│  ├── maintains current root folder / snapshot / filters                             │
│  └── coordinates background jobs                                                    │
│                                                                                      │
│  Services:                                                                           │
│  ├── ScanService                                                                    │
│  │   ├── Everything SDK integration                                                │
│  │   ├── scan index updates                                                        │
│  │   └── exclusion rules                                                           │
│  │                                                                                  │
│  ├── SnapshotService                                                                 │
│  │   ├── creates snapshot metadata                                                 │
│  │   ├── stores delta summaries                                                    │
│  │   └── applies threshold rules (default 50MB, hard floor)                        │
│  │                                                                                  │
│  ├── AnalysisService                                                                 │
│  │   ├── growth / shrink tracking                                                  │
│  │   ├── stale data detection                                                      │
│  │   ├── junk detection                                                            │
│  │   ├── disk full ETA forecasting                                                 │
│  │   └── snapshot compare engine                                                   │
│  │                                                                                  │
│  ├── DedupService                                                                    │
│  │   ├── size grouping                                                              │
│  │   ├── XXHash / fast hashing                                                      │
│  │   ├── duplicate cluster generation                                              │
│  │   └── hardlink / symlink suggestion                                             │
│  │                                                                                  │
│  ├── GraphService                                                                   │
│  │   ├── builds graph nodes/edges                                                  │
│  │   ├── hierarchical clustering                                                  │
│  │   ├── snapshot-aware graph states                                              │
│  │   └── lazy expansion                                                           │
│  │                                                                                  │
│  ├── VirtualTrashService                                                             │
│  │   ├── staging deletes                                                            │
│  │   └── projected reclaimed space                                                  │
│  │                                                                                  │
│  ├── PolicyService                                                                  │
│  │   ├── rule engine                                                               │
│  │   └── scheduled automation                                                      │
│  │                                                                                  │
│  └── ReportService                                                                  │
│      ├── PDF export                                                                 │
│      ├── CSV export                                                                 │
│      └── HTML export                                                                │
│                                                                                      │
└──────────────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────── DATA / STORAGE LAYER ────────────────────────────────┐
│                                                                                      │
│  SQLite Database (WAL mode)                                                          │
│  ├── files                                                                           │
│  ├── folders                                                                         │
│  ├── snapshots                                                                       │
│  ├── snapshot_folder_stats                                                           │
│  ├── file_hashes                                                                    │
│  ├── duplicate_groups                                                               │
│  ├── exclusion_rules                                                                │
│  ├── policies                                                                       │
│  ├── virtual_trash                                                                  │
│  └── report_history                                                                 │
│                                                                                      │
│  Optional cache:                                                                     │
│  ├── in-memory query cache                                                           │
│  └── graph layout cache                                                             │
└──────────────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────── INTEGRATION LAYER ───────────────────────────────────┐
│                                                                                      │
│  External / OS Integrations                                                          │
│  ├── Everything SDK                                                                  │
│  ├── Native file explorer actions                                                   │
│  ├── shell open / show in folder                                                    │
│  ├── recycle bin / delete                                                           │
│  └── optional scheduled task / background scan hook                                 │
└──────────────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────── BACKGROUND JOB / THREADING ──────────────────────────────┐
│                                                                                      │
│  Job Queue / Worker Pool                                                             │
│  ├── scanning jobs                                                                   │
│  ├── hash jobs                                                                       │
│  ├── snapshot jobs                                                                   │
│  ├── graph build jobs                                                                │
│  ├── report generation jobs                                                          │
│  └── forecast / analytics jobs                                                       │
│                                                                                      │
│  Rules:                                                                              │
│  - UI never blocks                                                                   │
│  - long tasks run async                                                              │
│  - updates sent back via signals/events                                              │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

---

# 2) Core Data Flow

```text
Everything SDK scan
        ↓
ScanService
        ↓
SQLite file/folder index
        ↓
SnapshotService creates snapshot summary
        ↓
AnalysisService computes deltas / stale / ETA / compare
        ↓
GraphService / TreeModel / HeatmapModel consume processed data
        ↓
UI updates views
        ↓
User actions → VirtualTrash / Dedup / Policies / Reports
```

---

# 3) Recommended Internal Modules

## UI modules
- `MainWindow`
- `TreePanel`
- `GraphPanel`
- `HeatmapPanel`
- `TimelinePanel`
- `DetailsPanel`
- `SettingsDialog`
- `ReportDialog`

## Core modules
- `AppController`
- `ScanService`
- `SnapshotService`
- `AnalysisService`
- `DedupService`
- `GraphService`
- `VirtualTrashService`
- `PolicyService`
- `ReportService`

## Data modules
- `DatabaseManager`
- `FileRepository`
- `SnapshotRepository`
- `GraphRepository`
- `PolicyRepository`

## Model/view modules
- `FolderTreeModel` (`QAbstractItemModel`)
- `GraphNodeModel`
- `SnapshotTimelineModel`
- `DetailsModel`

---

# 4) Suggested Build Order

1. **SQLite schema + database manager**
2. **Everything SDK scan ingestion**
3. **Tree view with virtualized model**
4. **Snapshot creation + comparison**
5. **Growth/shrink tracking**
6. **Graph view**
7. **Dedup engine**
8. **Virtual trash**
9. **Disk full ETA**
10. **Reports + policies**

---

# 5) Suggested Folder Structure

If you want Cursor to build cleanly, this structure works well:

```text
DiskAnalyzer/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── app/
│   │   ├── MainWindow.h/.cpp
│   │   └── AppController.h/.cpp
│   ├── ui/
│   │   ├── TreePanel.h/.cpp
│   │   ├── GraphPanel.h/.cpp
│   │   ├── HeatmapPanel.h/.cpp
│   │   ├── TimelinePanel.h/.cpp
│   │   ├── DetailsPanel.h/.cpp
│   │   └── ReportPanel.h/.cpp
│   ├── models/
│   │   ├── FolderTreeModel.h/.cpp
│   │   ├── GraphModel.h/.cpp
│   │   └── TimelineModel.h/.cpp
│   ├── services/
│   │   ├── ScanService.h/.cpp
│   │   ├── SnapshotService.h/.cpp
│   │   ├── AnalysisService.h/.cpp
│   │   ├── DedupService.h/.cpp
│   │   ├── GraphService.h/.cpp
│   │   ├── VirtualTrashService.h/.cpp
│   │   ├── PolicyService.h/.cpp
│   │   └── ReportService.h/.cpp
│   ├── data/
│   │   ├── DatabaseManager.h/.cpp
│   │   ├── FileRepository.h/.cpp
│   │   ├── FolderRepository.h/.cpp
│   │   ├── SnapshotRepository.h/.cpp
│   │   └── PolicyRepository.h/.cpp
│   ├── integrations/
│   │   ├── EverythingClient.h/.cpp
│   │   └── NativeFileOps.h/.cpp
│   └── utils/
│       ├── PathUtils.h/.cpp
│       ├── SizeUtils.h/.cpp
│       └── Thresholds.h/.cpp
└── resources/
    ├── icons/
    └── styles/
```



I wanna add this to the timeline system. Implement this and test it too.

# The Intelligent Storage Time-Machine

## Overview

Instead of capturing bulky, literal backups of the file system every week, the snapshot engine uses a user-configurable, **hybrid data lifecycle architecture**. It fuses a **Git-inspired Merkle Tree** (for structural history) with a **Rolling Transaction Log** (for high-fidelity short-term tracking).

This design provides a completely fluid, high-speed timeline navigation interface (a "Wayback Machine" for data) and an accurate simulation sandbox while keeping the local application database size remarkably flat.

---

## Technical Architecture: The Merkle Tree Ledger

To prevent storing thousands of redundant folder names and structural mappings weekly, directories are mapped using content-addressed normalization logic.

* **The Folder Registry:** Folder paths are written exactly once to a master lookup table (`master_folders`), where each path receives a permanent, unique `folder_id` (Integer).
* **The Structural Ledger:** Instead of recording full trees every week, each weekly snapshot simply inserts a 16-byte index row into a `snapshot_ledger` table:

$$\text{Snapshot ID (4 bytes)} + \text{Folder ID (4 bytes)} + \text{Aggregated Folder Size (8 bytes)}$$

* **Deduplication:** If a directory tree (e.g., a massive 50GB archived projects folder) does not change from Week 1 to Week 2, no new file rows are recorded. The Week 2 snapshot state simply points to the existing, unaltered structural index nodes.

---

## User-Configurable Resolution Routing & Filters

The ingestion engine processes paths from the Everything SDK through a **three-tier logical filter panel**. Users can completely customize which directories get granular tracking, which ones get macro tracking, and which ones are ignored entirely via the UI settings.

### 1. High-Resolution Tier (Changelog Whitelist)

* **Behavior:** Captures the macro Merkle tree structural checkpoints plus a transaction log of every single individual file event (`ADD`, `DELETE`, `MODIFY`, `RENAME`).
* **Utility:** Provides exact contextual change feeds (e.g., `[ADDED] family_video.mp4 (+2.1 GB)`).

### 2. Macro-Resolution Tier (Structural Only)

* **Behavior:** Records overall folder sizes in the Merkle Tree snapshot table but discards individual file transaction logging at the ingestion layer.
* **Utility:** Allows the user to see exactly how system or app sizes trend on the timeline without flooding the database or UI with millions of transient log files.

### 3. Absolute Blacklist (Ignore List)

* **Behavior:** Paths matching this list are completely skipped by the scanner. Zero bytes are recorded, bypassing both the Merkle Tree and the Changelog.
* **Utility:** Drops locked system files, temporary trash bins, or virtual memory blocks that artificially distort disk analysis.

---

## The "Average Joe" Sane Defaults Matrix

To ensure a seamless out-of-the-box experience, the application ships with pre-configured, battle-tested defaults mapped specifically to the behavior of a standard Windows operating system:

| Configuration Setting | Default Value / Paths | Purpose |
| --- | --- | --- |
| **Retention Window** | 30 Days *(Hard limits: Min 7 / Max 90)* | Balance between historical rollback memory and storage. |
| **High-Res Whitelist** | `C:\Users\<User>\Documents`<br>

<br>`C:\Users\<User>\Desktop`<br>

<br>`C:\Users\<User>\Downloads`<br>

<br>`C:\Users\<User>\Pictures`<br>

<br>`D:\*`, `E:\*` *(All secondary media drives)* | Prioritizes file-level Git-like tracking for files the user directly interacts with. |
| **Macro-Res System List** | `C:\Windows`<br>

<br>`C:\Program Files`<br>

<br>`C:\Program Files (x86)`<br>

<br>`C:\ProgramData`<br>

<br>`C:\Users\<User>\AppData\Roaming` | Tracks overall system expansion (like Windows Updates) without logging millions of temp files. |
| **Absolute Blacklist** | `C:\$Recycle.Bin`<br>

<br>`C:\System Volume Information`<br>

<br>`*pagefile.sys`<br>

<br>`*hiberfil.sys`<br>

<br>`*AppData\Local\Packages` *(Windows App Junk)*<br>

<br>`*AppData\Local\CrashDumps`<br>

<br>`*Google\Chrome\User Data\Default\Cache` | Prevents file-access permission crashes and strips out hyper-volatile web caches. |

---

## Lifecycle Policy: Customizable Rolling Compaction Window

To prevent the storage footprint from expanding indefinitely, the database utilizes an automated **Rolling Log Compaction** routing cycle tied to the user's custom retention preferences.

* **The Hot Retention Window (Customizable: 7 to 90 Days):** The database retains maximum data fidelity based on the user's configuration. Users can review exact file-level shifts across this active window.
* **The Cold Compaction Boundary:** An automated cleanup routine executes on a background thread during application shutdown. It locates rows in the transaction logs that have crossed the user's specified day limit (e.g., older than 30 days) and purges them.
* **The Compressed State Retention:** While the fine-grained transactional file records are safely deleted, the compressed folder snapshot weights remain intact inside the Merkle ledger. The historical timeline, line graphs, and treemaps continue to load accurately all the way back to Day 1.

---

## Database Footprint & Performance Metrics

By routing high-velocity system directories to the macro tier and enforcing a strict, customizable rolling compaction ceiling on whitelisted paths, the storage projection remains flat:

> ### Key Performance Benchmarks
> 
> 
> * **Transaction Changelog Cap:** Because system folders are stripped out and user paths are auto-compacted, the active transaction log table permanently maxes out at roughly **10 MB to 15 MB**. It will never grow past this threshold.
> * **Merkle Tree Index Core:** The structural layout expands conservatively at just **~12 MB to 25 MB per year** due to its content-addressed tree deduplication.
> * **Total 5-Year Database Size:** Instead of ballooning past 1 Gigabyte like unoptimized trackers, the entire database settles comfortably around **90 MB to 130 MB total**, providing professional enterprise-grade metrics on an average home computer.

build a plan first
