CREATE TABLE icon_asset (
  id TEXT PRIMARY KEY,
  mime TEXT NOT NULL,
  kind TEXT NOT NULL,
  bytes BLOB NOT NULL,
  created INTEGER NOT NULL,
  origin TEXT,
  byte_len INTEGER NOT NULL
);

CREATE TABLE icon_meta (
  library_id TEXT NOT NULL,
  name TEXT NOT NULL,
  label TEXT NOT NULL,
  keywords TEXT NOT NULL,
  popularity INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (library_id, name)
);
