#!/bin/bash
cd /Users/dimitarnanov/work/private/github/JSasta
for file in examples/kitchen-sink/*.js; do
  name=$(basename "$file")
  printf "%-40s" "$name"
  if ./build/jsastac "$file" 2>&1 | grep -q "Code generation complete"; then
    echo "✓"
  else
    echo "✗"
  fi
done
