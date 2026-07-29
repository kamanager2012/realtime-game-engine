# Security

This document summarizes the security posture of the engine and how to report
issues. It is written for operators and auditors, not end users.

> Scope note: the engine is published as **real-time multiplayer game
> infrastructure** with poker as the reference game. It is not, by itself, a
> turnkey real-money gambling product; deploy it only in environments and
> jurisdictions where your use is lawful, and prefer free-to-play / skill-based
> configurations.

## Current audit status

Result of a first-principles + adversarial audit of the release candidate:

| Severity | Outstanding |
|----------|-------------|
| Critical | 0 |
| High     | 0 |
| Medium   | 0 |

Historical findings from earlier audit rounds (fairness, money integrity,
auth, SQL, transport, anti-cheat) have all been remediated and covered by
regression tests (`Showdown_PotSplitConservation`, `StoredProofIsReplayableAndVerifiable`,
`ChipLedgerTest.Reconcile*`, etc.).

## Hardening summary

### Randomness & fairness
- 256-bit seed drawn from `RAND_bytes` (OS CSPRNG); never `std::rand`/`mt19937`
  for security.
- SHA-256 **commitment-reveal**: `commitment = SHA256(seed ‖ nonce)` published
  before the deal; `(seed, nonce)` revealed after, so the deck cannot be
  altered post-deal.
- Fisher-Yates driven by an **HMAC-SHA256 PRF** keyed by the full seed with
  bias-free rejection sampling (effective entropy 2²⁵⁶).
- See [FAIRNESS.md](FAIRNESS.md).

### Money integrity
- All chip/bet/pot math is `Chips` (`int64_t`). No floating point in the
  authoritative path; pot splits use integer division with remainder
  distributed one chip at a time, so `Σ payouts == pot` always.
- `ChipLedger` is the authoritative store: negative/zero-amount rejections,
  optimistic-lock (`UPDATE ... WHERE chips = ?`) concurrency guard, and
  `Reconcile()` which cross-checks every balance against the signed sum of
  `wallet_transactions`.

### Authentication & authorization
- Passwords: PBKDF2-HMAC-SHA256, **600,000** iterations, per-password CSPRNG
  salt.
- Password & token comparison: `CRYPTO_memcmp` (constant-time).
- JWT HS256 with `nlohmann/json` parsing, expiry enforcement, and per-player
  `RevokeAll` (token revocation by `iat`).
- No IDOR: WS actions use the connection's authenticated `player_id`; HTTP
  endpoints re-verify `verified_id == claimed_id`.

### Transport
- Auth token carried via `Authorization: Bearer` header or the WebSocket
  **subprotocol** — never the URL query string (eliminates proxy-log /
  Referer / history leakage).
- CORS: explicit origin allowlist (`POKER_ALLOWED_ORIGINS`), never `*`.
- WebSocket `Origin` validated on upgrade (CSWSH prevention).
- Rate limiting (per-IP connection / login / register / chat), 1 MB request
  body cap, 64 KB header cap, 64 KB WS frame cap, connection ceiling.

### Data access
- 100% parameterized SQL (`Prepare` + `StatementBinder`); no string
  concatenation of user input; the legacy `EscapeSQL` helper was removed.

### Anti-cheat
- Real-time, per-hand analysis (collusion + timing/action-cadence detection).
- Graduated automated response: warn → kick from table → permanent ban with
  token revocation **and** immediate teardown of the banned player's open
  WebSocket connections.
- Bans persisted to `bans.json` and reloaded on startup.

## Deployment guidance

- Terminate TLS at a reverse proxy (nginx/Envoy) in front of the server; the
  server speaks plaintext WS/HTTP. Configure the proxy to **not** log the
  request line if you keep any query parameters.
- Use `POKER_ALLOWED_ORIGINS` to restrict web origins.
- Source secrets (JWT signing key, admin token, DB credentials) from a secret
  manager — the K8s manifests reference HashiCorp Vault paths; do not commit
  them.
- Run with a non-privileged user; enable graceful shutdown.

## Reporting a vulnerability

Please report suspected vulnerabilities privately (do not open a public issue)
to the maintainers. Include:

- Affected component and version
- Steps to reproduce / proof-of-concept
- Impact and suggested mitigation

We will acknowledge, triage, and coordinate disclosure. Responsible disclosure
is appreciated; we aim to ship a fix before public disclosure.
