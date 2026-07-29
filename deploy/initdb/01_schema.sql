-- PostgreSQL analytics mirror schema (SQLite remains primary)
CREATE TABLE IF NOT EXISTS accounts (
  id BIGINT PRIMARY KEY,
  username TEXT NOT NULL,
  display_name TEXT,
  password_hash TEXT NOT NULL DEFAULT 'mirror',
  chips BIGINT DEFAULT 1000,
  elo_rating INTEGER DEFAULT 1500,
  total_profit BIGINT DEFAULT 0,
  hands_played BIGINT DEFAULT 0,
  avatar_url TEXT DEFAULT '',
  created_at TIMESTAMPTZ DEFAULT NOW(),
  last_login TIMESTAMPTZ DEFAULT NOW()
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_accounts_username ON accounts(username);

CREATE TABLE IF NOT EXISTS wallet_transactions (
  id BIGSERIAL PRIMARY KEY,
  player_id BIGINT NOT NULL REFERENCES accounts(id),
  tx_type TEXT NOT NULL,
  amount BIGINT NOT NULL,
  balance_after BIGINT NOT NULL,
  reference TEXT DEFAULT '',
  note TEXT DEFAULT '',
  created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_wallet_player ON wallet_transactions(player_id);
CREATE INDEX IF NOT EXISTS idx_wallet_created ON wallet_transactions(created_at);
