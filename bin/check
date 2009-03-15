#!/bin/bash

set -e

for f in "$@"
do
    if [ -f "$f" ]
    then
        echo $f
        ./run "$f" > "${f/%.na/.out}"
        diff "${f/%.na/.exp}" "${f/%.na/.out}"
    fi
done
