# Changelog

## [v1.0.0] — Production Release

Final pre-release adversarial audit (first-principles + red-team): **0 Critical, 0 High, 0 Medium outstanding**. All blocking and medium findings resolved.

### Resolved in final audit pass
- **CRITICAL — Pot split chip destruction**: `ShowdownEvaluator`/`PotManager` now use `Chips` (int64_t) throughout; uneven multi-way and odd pots split via exact integer division with the remainder distributed one chip at a time, so `Σ payouts == pot` is guaranteed. Added `Showdown_PotSplitConservation` regression test.
- **HIGH — Shuffle RNG**: Replaced the `std::mt19937` Fisher-Yates (only 64-bit entropy) with an HMAC-SHA256 PRF expansion keyed by the full 256-bit seed, with bias-free rejection sampling. Commitment-reveal replay still verifies the exact deck; effective entropy is now 2^256.
- **HIGH — Auth token in URL**: The WS auth token is no longer accepted from the URL query string. It is carried via the `Authorization: Bearer` header or the WebSocket subprotocol (browser-safe), eliminating leakage into proxy/edge access logs and browser history. Updated frontend + dev scripts.
- **MEDIUM — SQL parameterization**: `account_repository` (SaveAccount, FindByUsername, UpdateStats, DeleteAccount, UpdateDailyBonus, GetDailyBonus, UpdateLastLogin) now use prepared statements with `?` placeholders; the hand-built `EscapeSQL`/`QueryAccount(where_clause)` helpers are removed.
- **MEDIUM — Ban enforcement on live sessions**: A permanently banned player's still-open WebSocket connections are force-closed immediately (not just blocked on next login), and their tokens are revoked.
- **LOW — Admin token comparison**: `IsAdminRequest` now uses `CRYPTO_memcmp` constant-time comparison.

## [Unreleased] — Security Hardening & Production Readiness

### Security (14 Critical + 17 High + 14 Medium = 45 resolved)
- **CSPRNG**: Replaced `std::mt19937` with 256-bit OpenSSL `RAND_bytes` seed + SHA-256 commitment-reveal audit trail
- **JWT**: Removed default secret, replaced naive string parsing with `nlohmann::json`, added `CRYPTO_memcmp` constant-time comparison
- **Auth**: PBKDF2 increased from 10K to 600K iterations (OWASP 2024), implemented proper `RevokeAll` token revocation
- **SQL**: Removed `sqlEscape` function, all queries now parameterized via `Prepare()` + `StatementBinder`
- **Thread safety**: Added `std::recursive_mutex` to `GameState::ProcessAction` and `StartHand`
- **Timeout**: Implemented per-player action timeout with auto-fold (30s default)
- **Validation**: `ActionValidator` now rejects negative bet amounts
- **Anti-cheat**: Changed from per-20-hands to real-time per-hand analysis
- **Anti-cheat**: Added graduated intervention (warn→kick→ban) with automated response
- **CORS/WS**: Replaced CORS `*` with configurable origin whitelist, added WebSocket Origin validation
- **Timing**: Server-side action response time tracking for bot detection (stddev < 200ms detection)
- **Rate limiting**: Per-IP connection/login/registration throttling via token bucket
- **Request limits**: 1MB request body cap + 64KB header limit
- **Spectator**: Added configurable spectator delay and hole-card filtering
- **Admin**: Moved admin token from URL query param to Authorization header
- **Health**: Split `/health` (public) and `/health/detailed` (authenticated)
- **RBAC**: Added `PlayerRole` enum (Player/Moderator/Admin) to `PlayerAccount` and `AccountData`
- **GDPR**: Added `GET /api/account/export` data portability endpoint

### Financial Integrity
- **int64_t cents**: Replaced all `double` chip/bet/pot values with `Chips` (int64_t) — no floating-point rounding
- **Pot split conservation**: `Pot`/`ShowdownResult`/`EvaluatePot` are integer throughout; uneven splits distribute leftover chips one-per-winner so no chips are created or destroyed
- **Atomic buy-in**: Two-phase commit (debit→join confirmation or refund), with crash recovery on startup
- **Circuit breaker**: Redis client with exponential backoff and configurable failure threshold

### Architecture
- **Repository interface**: Added `IAccountRepository` abstract interface for DB swap/mocking
- **AI interface**: Implemented `IAIEngine` per ADR-004, added `CreateAIEngine` factory
- **HTTP router**: Extracted `http_router.h/cpp` module (transitional shim for monolith decomposition)
- **Network cleanup**: Deprecated duplicate `infra/net` WebSocket server, removed dead `poker_ws_server_v2` target
- **Naming**: Standardized `poker_engine_phase15-21` → `phase15-21` across all CMakeLists

### Performance
- **Preflop equity table**: Pre-computed 169×169 lookup table (5000 samples/matchup, ~1% error)
- **Optimized RNG**: Fisher-Yates shuffle driven by an HMAC-SHA256 PRF expansion over the full 256-bit CSPRNG seed (2^256 entropy, bias-free rejection sampling), replacing `std::mt19937`. Commitment-reveal audit replay preserved.

### Testing
- **Property-based tests**: 5 invariance checks (chips conserved, pot = sum bets, deck permutation, audit replay)
- **Fuzzing targets**: Card::Parse, JSON deserialization, Base64 decode, JWT payload
- **k6 load test**: WebSocket connection and HTTP health check script
- **Phase3 fix**: Array bounds bug in FlopExplorer::hero_wins_by_card (44→52)
- **CI ccache**: GitHub Actions build caching

### Deployment
- **Docker**: All ports bound to 127.0.0.1, Redis AUTH enabled, second instance gated
- **Kubernetes**: Removed hardcoded credentials from ConfigMap, switched Secrets to `stringData`, removed incompatible HPA
- **Security headers**: X-Content-Type-Options, X-Frame-Options added to HTTP responses
- **.dockerignore**: Excludes .git, build artifacts, *.db, node_modules

### Documentation
- **README.md**: Comprehensive with badges, architecture diagram, quick start
- **LICENSE**: MIT
- **CONTRIBUTING.md**: Code style, PR process, areas for contribution
- **infra/net/README.md**: Deprecation notice and migration plan

## [v1.4.0] — Pre-audit baseline (2026-06)
- Initial 21-phase architecture
- Core engine, WebSocket server, React frontend
- SQLite WAL persistence, PostgreSQL mirror
- CFR training, tournament support
- Docker and Kubernetes deployment manifests
