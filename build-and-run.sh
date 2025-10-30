#!/bin/bash
set -e

script=$( realpath $1 )
filename=$( basename $script )
output=${filename%.*}
output_ll=${filename%.*}.ll

make compiler -B
mkdir -p compiled
cd compiled
../build/jsastac $script $output_ll
clang -Wno-override-module -O3 -o ${output} ${output_ll}
./$output
