#!/bin/sh

prefix=`mktemp mallook-test.out.XXXXXX`

LD_PRELOAD=./libmallook.so MALLOOK_PREFIX="$prefix" ./mallook-test > "$prefix"

diff -u "$prefix" "$prefix".?*.?*.?*

rm "$prefix" "$prefix".?*.?*.?*
