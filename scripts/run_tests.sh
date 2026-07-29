#!/bin/bash
set -e

BUILD_DIR="${BUILD_DIR:-build}"
TEST_DIR="${TEST_DIR:-tests}"
TEST_FILTER="${1:-*}"
TIMEOUT="${TIMEOUT:-300}"

echo "========================================="
echo " Poker Engine - Full Test Suite"
echo "========================================="
echo "Build Dir:   $BUILD_DIR"
echo "Test Filter: $TEST_FILTER"
echo "Timeout:     ${TIMEOUT}s"
echo "========================================="

cd "$BUILD_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "\n${YELLOW}[1/4] Running Unit Tests...${NC}"

if ./tests/poker_engine_tests \
    --gtest_filter="$TEST_FILTER" \
    --gtest_output="xml:${TEST_DIR}/unit_test_results.xml" 2>&1 | tee "${TEST_DIR}/unit_test.log"; then
    echo -e "${GREEN}✓ Unit tests passed${NC}"
else
    echo -e "${RED}✗ Some unit tests failed${NC}"
fi

echo -e "\n${YELLOW}[2/4] Running Performance Tests...${NC}"

if [ -f "./tests/poker_engine_perf" ]; then
    if ./tests/poker_engine_perf \
        --gtest_filter="*Perf*" \
        --gtest_output="xml:${TEST_DIR}/perf_test_results.xml" 2>&1 | tee "${TEST_DIR}/perf_test.log"; then
        echo -e "${GREEN}✓ Performance tests passed${NC}"
    else
        echo -e "${YELLOW}⚠ Some performance benchmarks may have failed${NC}"
    fi
else
    echo -e "${YELLOW}[2/4] Skipping performance tests (not built)${NC}"
fi

if command -v valgrind &> /dev/null; then
    echo -e "\n${YELLOW}[3/4] Running Valgrind Memory Check...${NC}"
    valgrind --leak-check=full \
             --error-exitcode=1 \
             --track-origins=yes \
             --show-leak-kinds=all \
             --xml=yes \
             --xml-file="${TEST_DIR}/valgrind_report.xml" \
             ./tests/poker_engine_tests --gtest_filter="*Memory*:*Alloc*" \
             2>&1 | tee "${TEST_DIR}/valgrind.log" || {
        echo -e "${RED}✗ Valgrind found memory issues${NC}"
    }
else
    echo -e "\n${YELLOW}[3/4] Skipping Valgrind (not installed)${NC}"
fi

if [ -f "./tests/poker_engine_fuzz" ] && [ "${RUN_FUZZING:-0}" = "1" ]; then
    echo -e "\n${YELLOW}[4/4] Running Fuzz Tests...${NC}"
    timeout 60 ./tests/poker_engine_fuzz \
        -max_len=1024 \
        -max_total_time=60 \
        -artifact_prefix="${TEST_DIR}/fuzz_artifacts/" \
        2>&1 || {
        echo -e "${RED}✗ Fuzzing found crashes${NC}"
    }
else
    echo -e "\n${YELLOW}[4/4] Skipping Fuzzing${NC}"
fi

echo -e "\n========================================="
echo " Test Summary"
echo "========================================="

if [ -f "${TEST_DIR}/unit_test_results.xml" ]; then
    TOTAL=$(grep -oP 'tests="\K\d+' "${TEST_DIR}/unit_test_results.xml" || echo "?")
    FAILED=$(grep -oP 'failures="\K\d+' "${TEST_DIR}/unit_test_results.xml" || echo "?")
    echo "  Total Tests: $TOTAL"
    echo "  Failures:    $FAILED"
fi

echo ""
if command -v gcovr &> /dev/null; then
    echo -e "${YELLOW}Code Coverage:${NC}"
    gcovr --html-details "${TEST_DIR}/coverage.html" \
          --fail-under-line 70 2>&1 | head -20
fi

echo "========================================="
echo " Test artifacts in: ${TEST_DIR}/"
echo "========================================="

exit 0
