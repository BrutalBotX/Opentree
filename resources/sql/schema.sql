CREATE TABLE IF NOT EXISTS folders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL UNIQUE,
    parent_path TEXT,
    name TEXT NOT NULL,
    total_size INTEGER NOT NULL DEFAULT 0,
    file_count INTEGER NOT NULL DEFAULT 0,
    last_scan_root TEXT
);

CREATE TABLE IF NOT EXISTS files (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    root_path TEXT NOT NULL,
    path TEXT NOT NULL UNIQUE,
    parent_path TEXT NOT NULL,
    name TEXT NOT NULL,
    size INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS snapshots (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    created_at TEXT NOT NULL,
    root_path TEXT NOT NULL,
    total_size INTEGER NOT NULL DEFAULT 0,
    file_count INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS snapshot_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    snapshot_id INTEGER NOT NULL,
    kind TEXT NOT NULL,
    path TEXT NOT NULL,
    parent_path TEXT,
    name TEXT NOT NULL,
    size INTEGER NOT NULL DEFAULT 0,
    parent_size INTEGER NOT NULL DEFAULT 0,
    file_count INTEGER NOT NULL DEFAULT 0,
    folder_count INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(snapshot_id) REFERENCES snapshots(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS snapshot_current_files (
    root_path TEXT NOT NULL,
    path TEXT NOT NULL,
    parent_path TEXT NOT NULL,
    name TEXT NOT NULL,
    size INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(root_path, path)
);

CREATE TABLE IF NOT EXISTS snapshot_file_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    snapshot_id INTEGER NOT NULL,
    event_type TEXT NOT NULL,
    path TEXT NOT NULL,
    parent_path TEXT NOT NULL,
    name TEXT NOT NULL,
    old_size INTEGER NOT NULL DEFAULT 0,
    new_size INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY(snapshot_id) REFERENCES snapshots(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS snapshot_background_runs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    started_at TEXT NOT NULL,
    finished_at TEXT NOT NULL,
    status TEXT NOT NULL,
    roots_processed INTEGER NOT NULL DEFAULT 0,
    snapshots_created INTEGER NOT NULL DEFAULT 0,
    events_compacted INTEGER NOT NULL DEFAULT 0,
    message TEXT
);

CREATE INDEX IF NOT EXISTS idx_folders_parent_path ON folders(parent_path);
CREATE INDEX IF NOT EXISTS idx_files_parent_path ON files(parent_path);
CREATE INDEX IF NOT EXISTS idx_snapshot_items_snapshot_id ON snapshot_items(snapshot_id);
CREATE INDEX IF NOT EXISTS idx_snapshot_file_events_snapshot_id ON snapshot_file_events(snapshot_id);

-- Virtual trash: staged deletion intents. Staging only writes a row here and nothing on
-- disk is touched until the user explicitly confirms moving the staged items to the
-- Recycle Bin.
CREATE TABLE IF NOT EXISTS virtual_trash (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL UNIQUE,
    size INTEGER NOT NULL DEFAULT 0,
    is_folder INTEGER NOT NULL DEFAULT 0,
    root_path TEXT,
    staged_at TEXT NOT NULL,
    reason TEXT
);

CREATE INDEX IF NOT EXISTS idx_virtual_trash_staged_at ON virtual_trash(staged_at);

-- Merkle-style structural ledger. Folder paths are registered once in master_folders and
-- every snapshot stores one small row per tracked folder, so unchanged trees cost nothing
-- beyond the shared folder rows.
CREATE TABLE IF NOT EXISTS master_folders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS snapshot_ledger (
    snapshot_id INTEGER NOT NULL,
    folder_id INTEGER NOT NULL,
    total_size INTEGER NOT NULL DEFAULT 0,
    file_count INTEGER NOT NULL DEFAULT 0,
    folder_count INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(snapshot_id, folder_id),
    FOREIGN KEY(snapshot_id) REFERENCES snapshots(id) ON DELETE CASCADE,
    FOREIGN KEY(folder_id) REFERENCES master_folders(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_snapshot_ledger_folder ON snapshot_ledger(folder_id);

-- Per-path scan resolution: 2 = high resolution (structural + file events), 1 = macro
-- (structural sizes only), 0 = blacklist (skipped by the scanner).
CREATE TABLE IF NOT EXISTS resolution_rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL UNIQUE,
    tier INTEGER NOT NULL DEFAULT 2
);
