#!/bin/bash
set -e

script=$( realpath $1 )
filename=$( basename $script )
output=${filename%.*}
output_ll=${filename%.*}.ll

make -B
mkdir -p compiled
cd compiled
../build/jscompiler $script $output_ll -a
clang -o ${output} ${output_ll}
./$output
