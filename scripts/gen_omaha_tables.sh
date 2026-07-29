#!/usr/bin/env bash
# Generates / fetches the Omaha (PLO4/5/6) lookup tables for PokerHandEvaluator.
#
# These tables are LARGE pre-computed lookup tables and are intentionally OMITTED
# from the distributed repository to keep clone size small. Omaha support is
# therefore OPT-IN. Texas Hold'em (the reference implementation) needs none of
# these tables and works out of the box.
#
# After running this script and reconfiguring CMake, PHEVAL_HAVE_PLO is defined
# automatically and the Omaha evaluator becomes available.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../third_party/PokerHandEvaluator/src" && pwd)"
cd "$SRC_DIR"

need=(tables_plo4.c tables_plo5.c tables_plo6.c)
missing=0
for f in "${need[@]}"; do
  if [[ -f "$f" ]]; then
    echo "[gen_omaha_tables] present: $f"
  else
    echo "[gen_omaha_tables] MISSING: $f"
    missing=1
  fi
done

if [[ "$missing" -eq 0 ]]; then
  echo "[gen_omaha_tables] All Omaha tables present. Nothing to do."
  echo "[gen_omaha_tables] Reconfigure CMake to enable PHEVAL_HAVE_PLO."
  exit 0
fi

# The upstream PokerHandEvaluator project ships these tables. To enable Omaha:
#   1. Obtain tables_plo4.c, tables_plo5.c, tables_plo6.c from the upstream
#      PokerHandEvaluator release (or generate them with its build tooling), and
#   2. Place them in this directory (third_party/PokerHandEvaluator/src/), then
#   3. Re-run CMake so PHEVAL_HAVE_PLO is picked up.
#
# If you downloaded the .tar.gz variants (tables_plo5.tar.gz / tables_plo6.tar.gz)
# instead of the .c files, extract them here first:
if [[ -f tables_plo5.tar.gz ]]; then tar -xzf tables_plo5.tar.gz; fi
if [[ -f tables_plo6.tar.gz ]]; then tar -xzf tables_plo6.tar.gz; fi

echo
echo "[gen_omaha_tables] Tables still missing after extraction attempt."
echo "[gen_omaha_tables] Please place the Omaha table files manually as described above,"
echo "[gen_omaha_tables] then reconfigure the build. Hold'em remains fully functional."
exit 1
