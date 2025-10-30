#!/bin/bash

# Module system test suite

COMPILER="../../build/jsastac"
PASS=0
FAIL=0

echo "======================================"
echo "Module System Test Suite"
echo "======================================"
echo ""

# Test 1: Basic import
echo "Test 1: Basic import (main -> math)"
if $COMPILER main.jsa 2>&1 | grep -q "No diagnostics"; then
    echo "✓ PASS: Compilation successful"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: Compilation failed"
    $COMPILER main.jsa
    FAIL=$((FAIL + 1))
fi
echo ""

# Test 2: Nested imports
echo "Test 2: Nested imports (main -> utils -> math)"
if $COMPILER test_nested.jsa 2>&1 | grep -q "No diagnostics"; then
    echo "✓ PASS: Compilation successful"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: Compilation failed"
    $COMPILER test_nested.jsa
    FAIL=$((FAIL + 1))
fi
echo ""

# Test 3: Multiple imports
echo "Test 3: Multiple imports (main -> math, strings)"
if $COMPILER test_multiple.jsa 2>&1 | grep -q "No diagnostics"; then
    echo "✓ PASS: Compilation successful"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: Compilation failed"
    $COMPILER test_multiple.jsa
    FAIL=$((FAIL + 1))
fi
echo ""

# Test 4: Cyclic import detection
echo "Test 4: Cyclic import detection"
if $COMPILER test_cycle.jsa 2>&1 | grep -q "Cyclic import detected"; then
    echo "✓ PASS: Cyclic import correctly detected"
    PASS=$((PASS + 1))
else
    echo "✗ FAIL: Cyclic import not detected"
    $COMPILER test_cycle.jsa
    FAIL=$((FAIL + 1))
fi
echo ""

# Summary
echo "======================================"
echo "Test Results: $PASS passed, $FAIL failed"
echo "======================================"

exit $FAIL
