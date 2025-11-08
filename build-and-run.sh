#!/bin/bash
set -e

script=$( realpath $1 )
filename=$( basename $script )
output=compiled/${filename%.*}
output_ll=${filename%.*}.ll

DEBUG=1 make compiler
mkdir -p compiled
cd compiled
../build/debug/jsastac $script $output_ll
clang -Wno-override-module -O3 -o ${output} ${output_ll}
./$output
