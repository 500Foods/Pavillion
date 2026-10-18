CREATE TABLE IF NOT EXISTS activity (
  id INTEGER PRIMARY KEY,
  kind TEXT NOT NULL,
  account_id TEXT,
  title TEXT NOT NULL,
  detail TEXT,
  state TEXT NOT NULL,
  started_ms INTEGER NOT NULL,
  ended_ms INTEGER,
  prog_done INTEGER,
  prog_total INTEGER,
  prog_unit TEXT,
  eta_ms INTEGER,
  cancellable INTEGER NOT NULL,
  error TEXT,
  parent_id INTEGER,
  dismissed INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE account_state (
  account_id TEXT PRIMARY KEY,
  session_json TEXT,
  session_state TEXT,
  session_fetched INT,
  state TEXT NOT NULL DEFAULT 'idle',
  last_error TEXT,
  last_sync INT
);
CREATE TABLE remote_account (
  id TEXT PRIMARY KEY,
  account_id TEXT NOT NULL REFERENCES account_state ON DELETE CASCADE,
  jmap_account_id TEXT NOT NULL,
  name TEXT,
  is_personal INT,
  caps TEXT,
  UNIQUE (account_id, jmap_account_id)
);
CREATE TABLE identity (
  ra_id TEXT REFERENCES remote_account ON DELETE CASCADE,
  id TEXT,
  name TEXT,
  email TEXT,
  reply_to TEXT,
  bcc TEXT,
  sig_html TEXT,
  sig_text TEXT,
  may_delete INT,
  PRIMARY KEY (ra_id, id)
);
CREATE TABLE mailbox (
  ra_id TEXT REFERENCES remote_account ON DELETE CASCADE,
  id TEXT,
  parent_id TEXT,
  name TEXT,
  role TEXT,
  sort_order INT,
  total INT,
  unread INT,
  total_threads INT,
  unread_threads INT,
  my_rights TEXT,
  is_subscribed INT,
  PRIMARY KEY (ra_id, id)
);
CREATE TABLE sync_state (
  ra_id TEXT REFERENCES remote_account ON DELETE CASCADE,
  type TEXT,
  state TEXT,
  updated INT,
  PRIMARY KEY (ra_id, type)
);
CREATE TABLE kv (
  key TEXT PRIMARY KEY,
  value TEXT
);
