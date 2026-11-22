#!/bin/bash
# Runs the framework's checks: the build is the caller's job (BUILDING.md), then the unit tests, then (by default) the
# negative suite, which breaks a copy of the framework on purpose and requires the unit tests to notice (a few minutes).
#   --skip-negative   leaves out the negative checks inside the unit tests and the negative suite (a few seconds)
cd "$(dirname "$0")" || exit 2
SKIP=()
for option in "$@"; do
  case $option in
    --skip-negative) SKIP=(--skip-negative) ;;
    *) echo "unknown option: $option (the only option is --skip-negative)"; exit 2 ;;
  esac
done
failed=0
echo "=== check-unit"; ./check-unit.sh "${SKIP[@]}" || failed=1
echo; echo "=== negative-check (breaks the framework on purpose)"
if [ ${#SKIP[@]} -gt 0 ]; then echo "SKIPPED (--skip-negative)"; else ./negative-tests/negative-check.sh || failed=1; fi
echo; [ $failed = 0 ] && echo "ALL CHECKS PASSED" || { echo "SOME CHECKS FAILED"; exit 1; }
