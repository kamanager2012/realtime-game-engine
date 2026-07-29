# ADR-006: Vendored C++ Dependencies

| 字段         | 值                                      |
|-------------|----------------------------------------|
| 编号        | 006                                     |
| 状态        | Accepted                                |
| 决策日期    | 2026-06-22                              |
| 被替代      | —                                       |

## Context

The project previously used CMake `FetchContent` to download dependencies (nlohmann/json, spdlog, fmt, googletest) at configure time. This caused build failures in:

- **Air-gapped / offline environments** — no network access during CI or development
- **Corporate firewalls** — proxy restrictions block CMake's HTTP downloads
- **Reproducibility concerns** — FetchContent pins tags but still hits the network; a remote tag change or repo deletion breaks the build
- **Build latency** — first-time FetchContent downloads add 30-60s to configure

The project also had duplicate header directories (`core/base/include/`, `core/evaluator/include/`, `core/range/include/`) that shadowed the canonical `core/include/` headers, causing confusion about which version was compiled.

## Decision

1. **Vendor all C++ dependencies** into `third_party/`:
   - `nlohmann/json.hpp` → `third_party/nlohmann/include/`
   - `spdlog` headers → `third_party/spdlog/include/`
   - `fmt` headers → `third_party/fmt/include/`
   - `googletest` prebuilt → `third_party/googletest/`
   - `PokerHandEvaluator` → `third_party/PokerHandEvaluator/`
   - `httplib.h` → `third_party/httplib.h`

2. **CMakeLists.txt uses `find_package` with fallback** to vendored INTERFACE libraries:
   ```cmake
   find_package(nlohmann_json QUIET)
   if(NOT nlohmann_json_FOUND)
       add_library(nlohmann_json INTERFACE)
       target_include_directories(nlohmann_json SYSTEM INTERFACE
           ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/include)
   endif()
   ```

3. **Remove dead duplicate directories**: `core/base/`, `core/evaluator/`, `core/range/`, `.orig/`

4. **Canonical header location**: `core/include/` is the single source of truth for all core headers

## Consequences

### Positive
- **Offline builds work** — no network required after clone
- **Faster configure** — no download step, saves 30-60s
- **Reproducible builds** — dependency versions are pinned in the repo
- **No shadowing** — single header location per module eliminates include-path confusion
- **`find_package` fallback** — system packages still preferred when available (e.g., distro packages)

### Negative
- **Repo size increase** — vendored headers add ~2MB (acceptable for a C++ project)
- **Manual updates** — dependency upgrades require copying new headers into `third_party/` rather than changing a CMake tag
- **No version pinning in CMake** — the fallback path doesn't encode the version number; version info is implicit in the vendored files

### Mitigations
- Document vendored versions in `third_party/CMakeLists.txt` comments
- Periodically update vendored deps (quarterly or on security advisory)
- The `find_package` fallback means distro-packaged versions are used when available, reducing the vendored path's blast radius
