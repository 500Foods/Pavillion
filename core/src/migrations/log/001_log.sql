CREATE TABLE session (
  id INTEGER PRIMARY KEY,
  started INTEGER NOT NULL,
  source TEXT NOT NULL,
  label TEXT
);
CREATE TABLE log (
  seq INTEGER PRIMARY KEY,
  ts INTEGER NOT NULL,
  session INTEGER NOT NULL,
  level INTEGER NOT NULL,
  subsystem TEXT NOT NULL,
  text TEXT NOT NULL
);
CREATE INDEX log_ts ON log (ts);
CREATE INDEX log_session_seq ON log (session, seq);
CREATE INDEX log_level_seq ON log (level, seq);
