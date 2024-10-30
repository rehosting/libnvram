#!/bin/bash

NVRAM_O_PATH=$1
OUT_PATH=$2

global_symbols=$(nm --defined-only -gP $NVRAM_O_PATH | awk '{print $1 ";"}')

# Print the global_symbols for debugging
echo "Global symbols:"
echo "$global_symbols"

cat <<EOF > $OUT_PATH
{
    global:
$global_symbols
    local:
        *;
}
EOF