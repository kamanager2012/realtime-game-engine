# Security Policy

This repository's full security documentation lives under [`docs/SECURITY.md`](docs/SECURITY.md), which covers:

- **Audit status** — release candidate audited to **0 Critical / 0 High / 0 Medium**.
- **Hardening summary** — provably-fair RNG, integer money integrity, auth, transport, data access, and anti-cheat.
- **Deployment guidance** — TLS termination, origin allowlist, secret management.

Companion documents:

- [Provably-fair RNG design](docs/FAIRNESS.md)
- [Threat model](docs/THREAT_MODEL.md)

## Reporting a vulnerability

Please report suspected vulnerabilities **privately** (do not open a public issue). Include the affected component and version, steps to reproduce, impact, and a suggested mitigation. We will acknowledge, triage, and coordinate disclosure.

> Scope note: this engine is published as real-time multiplayer game infrastructure with poker as the reference game. It is not, by itself, a turnkey real-money gambling product; deploy it only where your use is lawful.
