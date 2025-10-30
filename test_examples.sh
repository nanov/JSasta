#!/bin/bash

COMPILER="./build/jsastac"
PASS=0
FAIL=0

echo "======================================"
echo "JSasta Examples Test Suite"
echo "======================================"
echo ""

# Test each .jsa file in examples
for file in examples/*.jsa; do
    if [ -f "$file" ]; then
        echo "Testing: $file"
        if $COMPILER "$file" /tmp/test_output.ll 2>&1 | grep -q "Code generation complete"; then
            echo "✓ PASS"
            PASS=$((PASS + 1))
        else
            echo "✗ FAIL"
            $COMPILER "$file" /tmp/test_output.ll 2>&1 | tail -5
            FAIL=$((FAIL + 1))
        fi
        echo ""
    fi
done

# Test array examples
for file in examples/array/*.jsa; do
    if [ -f "$file" ]; then
        echo "Testing: $file"
        if $COMPILER "$file" /tmp/test_output.ll 2>&1 | grep -q "Code generation complete"; then
            echo "✓ PASS"
            PASS=$((PASS + 1))
        else
            echo "✗ FAIL"
            $COMPILER "$file" /tmp/test_output.ll 2>&1 | tail -5
            FAIL=$((FAIL + 1))
        fi
        echo ""
    fi
done

echo "======================================"
echo "Examples: $PASS passed, $FAIL failed"
echo "======================================"

# Run module tests
cd examples/modules && bash run_tests.sh
MODULE_RESULT=$?

if [ $MODULE_RESULT -eq 0 ] && [ $FAIL -eq 0 ]; then
    echo ""
    echo "======================================"
    echo "ALL TESTS PASSED!"
    echo "======================================"
    exit 0
else
    exit 1
fi
