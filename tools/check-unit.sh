#!/bin/bash
# The unit tests of the framework: attribute value types, and the framework's behaviour as seen through tfprotov6 (the
# same calls Core makes; no gRPC, no OpenTofu). Runs build/unit-tests (source in tests/). Details: TESTING.md.
#   --skip-negative   leave out the checks that expect an error (bad input, invalid configuration)
UNIT_TESTS=${UNIT_TESTS:-$(cd "$(dirname "$0")/.." && pwd)/build/unit-tests}
[ -x "$UNIT_TESTS" ] || { echo "no $UNIT_TESTS: build the project first (BUILDING.md)"; exit 2; }
exec "$UNIT_TESTS" "$@"
