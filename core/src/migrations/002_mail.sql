CREATE TABLE email (rowid INTEGER PRIMARY KEY, ra_id TEXT NOT NULL REFERENCES remote_account ON DELETE CASCADE, id TEXT NOT NULL,
  thread_id TEXT, blob_id TEXT, message_id TEXT, in_reply_to TEXT, refs TEXT,
  subject TEXT, from_json TEXT, to_json TEXT, cc_json TEXT, bcc_json TEXT, reply_to_json TEXT,
  sent_at INT, received_at INT, size INT, preview TEXT, has_attachment INT,
  seen INT, flagged INT, answered INT, forwarded INT, draft INT, junk INT, keywords TEXT,
  body_state TEXT NOT NULL DEFAULT 'none',
  last_opened INT, local_marks TEXT,
  UNIQUE (ra_id, id));
CREATE INDEX email_thread ON email(ra_id, thread_id);
CREATE INDEX email_recv ON email(received_at DESC);
CREATE TABLE email_mailbox (
  email_rowid INT NOT NULL REFERENCES email ON DELETE CASCADE,
  ra_id TEXT NOT NULL,
  mailbox_id TEXT NOT NULL,
  received_at INT NOT NULL,
  PRIMARY KEY (email_rowid, ra_id, mailbox_id),
  FOREIGN KEY (ra_id, mailbox_id) REFERENCES mailbox (ra_id, id) ON DELETE CASCADE);
CREATE INDEX em_list ON email_mailbox(ra_id, mailbox_id, received_at DESC, email_rowid);
CREATE TABLE thread (ra_id TEXT, id TEXT, email_ids TEXT, PRIMARY KEY (ra_id, id));
CREATE TABLE body_cache (email_rowid INT PRIMARY KEY REFERENCES email ON DELETE CASCADE,
  text_plain_path TEXT, html_raw_path TEXT, html_sanitized_path TEXT,
  has_remote INT, sanitizer_version INT, fetched_at INT);
CREATE TABLE attachment (email_rowid INT REFERENCES email ON DELETE CASCADE, part_id TEXT,
  blob_id TEXT, name TEXT, mime TEXT, size INT, cid TEXT, disposition TEXT, local_path TEXT,
  PRIMARY KEY (email_rowid, part_id));
CREATE VIRTUAL TABLE email_fts USING fts5(subject, from_text, to_text, body,
  content='', tokenize='unicode61 remove_diacritics 2');
CREATE TABLE remote_allow (pattern TEXT PRIMARY KEY, kind TEXT NOT NULL);
CREATE TABLE retention_rule (ra_id TEXT, mailbox_id TEXT, keep_newest INT, max_age_days INT,
  action TEXT NOT NULL DEFAULT 'trash', last_run INT, PRIMARY KEY (ra_id, mailbox_id));
CREATE TABLE op_queue (id INTEGER PRIMARY KEY, ra_id TEXT, kind TEXT, payload TEXT,
  base_state TEXT, created INT, not_before INT, attempts INT DEFAULT 0, last_error TEXT,
  status TEXT NOT NULL DEFAULT 'pending', undo_payload TEXT);
CREATE TABLE outbox (id INTEGER PRIMARY KEY, ra_id TEXT, identity_id TEXT, draft_email_id TEXT,
  mime_path TEXT, envelope_json TEXT, send_at INT, status TEXT, attempts INT, last_error TEXT, created_by TEXT);
CREATE TABLE draft_local (id TEXT PRIMARY KEY, ra_id TEXT, json TEXT, server_email_id TEXT, updated INT, saved_server INT);
CREATE TABLE recent_address (email TEXT PRIMARY KEY, name TEXT, last_used INT, count INT);
