# Contributing to Poker Engine

Thanks for your interest in contributing.

## Code Style

- **C++20** with `-Wall -Wextra -Wpedantic`
- No exceptions for flow control — prefer `std::optional` / `std::expected`
- Prefer `std::unique_ptr` over raw/shared ownership
- `const` correctness on all methods
- Namespaces: `poker_engine::<domain>` (e.g. `poker_engine::game`, `poker_engine::ai`)

## Build & Test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/tests/poker_tests
```

All 405 tests must pass before submitting.

## Pull Request Process

1. Fork the repository
2. Create a feature branch
3. Add tests for new functionality
4. Ensure all tests pass (`./build/tests/poker_tests`)
5. Submit PR with description of changes

## Areas for Contribution

- **AI**: CFR training improvements, new bot strategies, equity analysis
- **Variants**: Short Deck (6+), Pineapple, 5-Card Omaha
- **Frontend**: UX improvements, mobile support, accessibility
- **Performance**: Evaluation lookup tables, protocol optimization
- **Testing**: Fuzzing harnesses, property-based tests, load testing

## Security

If you discover a security vulnerability, please do NOT open a public issue. Contact the maintainers directly.
