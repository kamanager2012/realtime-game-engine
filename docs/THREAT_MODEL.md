# Threat Model

Assumed adversary and the mitigations the engine provides. This is the
operational companion to [SECURITY.md](SECURITY.md) and [FAIRNESS.md](FAIRNESS.md).

## Assets to protect

- **Player balances** (chips) — the authoritative ledger.
- **Game integrity** — dealt cards and payout outcomes.
- **Player credentials / sessions** — accounts, JWTs.
- **Audit trail** — hand history, event log, ledger (for dispute resolution).
- **Service availability** — the WebSocket/HTTP endpoint.

## Adversary model

| # | Adversary | Capability | Mitigation |
|---|-----------|------------|------------|
| A1 | **Cheating player** | sees others' hole cards, colludes, times actions, bots | provably-fair shuffle (no card preview); per-hand anti-cheat (collusion + cadence); bans revoke tokens **and** tear down live sockets |
| A2 | **Network eavesdropper** | sniffs traffic | token in `Authorization`/subprotocol, never URL; run TLS at proxy |
| A3 | **Malicious operator** | wants to deal favored cards / skim chips | commitment-reveal + replay verification (A2 in FAIRNESS.md); integer ledger with `Reconcile()` detects balance drift |
| A4 | **SQL injection attacker** | crafted input | 100% parameterized queries; no string concatenation of input |
| A5 | **Credential attacker** | password guessing, token theft | PBKDF2 600k, constant-time compare, login lockout, JWT revocation, short-lived tokens |
| A6 | **Origin/CSWSH attacker** | cross-site WS hijack | CORS allowlist + `Origin` check on upgrade |
| A7 | **Resource-exhaustion attacker** | floods, giant frames/bodies | per-IP rate limits, 1 MB body / 64 KB header / 64 KB WS frame caps, connection ceiling |
| A8 | **Insider / log leakage** | token in proxy logs | token never in URL (see A2) |

## What is explicitly out of scope / accepted risk

- **Transport encryption** is delegated to a fronting reverse proxy (nginx/
  Envoy). The server itself speaks plaintext; deploy TLS at the edge.
- **Client-seeded commitment** (player contributes entropy to the shuffle) is
  a future enhancement; the current commitment is server-only (see FAIRNESS.md).
- **`bans.json`** is plaintext with no integrity protection — tampering
  requires filesystem write access (already full compromise), and is logged.
- **Unauthenticated read endpoints** (hand history / player stats) expose
  semi-public data by design (leaderboards); restrict if your policy requires.
- **Distributed deployment** (Redis session store) inherits Redis's trust
  boundary; secure the Redis link and use the provided circuit breaker.

## Defense-in-depth checklist (deploy time)

- [ ] TLS terminated at reverse proxy; proxy does not log request lines.
- [ ] `POKER_ALLOWED_ORIGINS` set to your frontend domain(s).
- [ ] Secrets (JWT key, admin token, DB creds) from Vault / secret manager,
      never committed.
- [ ] Rate limits tuned to your traffic; connection ceiling set.
- [ ] Prometheus metrics + alerting on error rate and latency.
- [ ] Backups of the ledger DB; periodic `Reconcile()` run in health checks.
- [ ] Ban list monitored; `bans.json` on persisted, access-controlled storage.

## Incident response

1. Suspected cheat → anti-cheat auto-responds (kick/ban) and writes an audit
   log entry; review `bans.json` and the structured audit log.
2. Suspected balance drift → run `ChipLedger::Reconcile()`; mismatch indicates
   a bug or tampering — freeze writes and investigate the event log.
3. Suspected shuffle bias → verify a hand's `HandProof` (FAIRNESS.md). A
   commitment mismatch means the server was compromised.
4. Report per [SECURITY.md](SECURITY.md).
