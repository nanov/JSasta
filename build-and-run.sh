#!/bin/bash
set -e

script=$( realpath $1 )
filename=$( basename $script )
output_name=${filename%.*}

DEBUG=1 make compiler
mkdir -p compiled
cd compiled
# Use -d for debug mode instead of -DDEBUG (which is a C preprocessor flag)
../build/debug/jsastac -O0 -g --sanitize=address -d -o $output_name $script
./$output_name
