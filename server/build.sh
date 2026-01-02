#!/bin/bash

# ItsHighNoon's C build script
#
# Last modified 11/27/2025

readarray -t flags < compile_flags.txt
echo "Using flags: $(IFS=$' '; echo "${flags[*]}")"

source_files=()
while IFS= read -r line; do
    source_files+=("${line#src/}")
done < <(find "src" -type f -name "*.c")

rm -rf obj
mkdir -p obj
object_files=()
for source in "${source_files[@]}"; do
    object="obj/${source%.*}.o"
    object_files+=("$object")
    echo "Building $source"
    dir="${object%/*}"
    mkdir -p $dir
    clang -c -o "$object" "src/$source" $(IFS=$'\n'; echo "${flags[*]}") &
done
wait

echo "Linking"
clang $(IFS=$'\n'; echo "${flags[*]}") -o "server" $(IFS=$'\n'; echo "${object_files[*]}")

echo "Build done, doing static analysis"
mkdir -p analysis
source_files=()
while IFS= read -r line; do
    source_files+=("${line#src/}")
done < <(find "src" -type f -name "*.c")
for source in "${source_files[@]}"; do
    plist="analysis/${source%.*}.plist"
    echo "Analyzing $source"
    dir="${plist%/*}"
    mkdir -p $dir
    clang --analyze "src/$source" $(IFS=$'\n'; echo "${flags[*]}") -o $plist
done
wait
echo "Static analysis done"
