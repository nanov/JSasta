#!/bin/bash

# Regenerate test fixtures for a specific suite or all suites
# Usage: ./scripts/regen-fixtures.sh [suite_name]
# Example: ./scripts/regen-fixtures.sh structs

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

if [ ! -f "build/jsastat" ]; then
    echo "Error: Test runner not found. Run 'make' first."
    exit 1
fi

if [ $# -eq 0 ]; then
    echo "Regenerating fixtures for ALL test suites..."
    ./build/jsastat tests/compiler -u
else
    SUITE=$1
    SUITE_PATH="tests/compiler/$SUITE"

    if [ ! -d "$SUITE_PATH" ]; then
        echo "Error: Suite '$SUITE' not found at $SUITE_PATH"
        echo "Available suites:"
        ls -1 tests/compiler | grep -v "\.jsa$"
        exit 1
    fi

    echo "Regenerating fixtures for suite: $SUITE"
    ./build/jsastat "$SUITE_PATH" -u
fi

echo "Done!"
