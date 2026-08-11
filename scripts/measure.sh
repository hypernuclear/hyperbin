#!/bin/sh
# Measure the Dock's drawn trash icon against its Accessibility rect.
# Prints the ratios the overlay needs to place its copy of the artwork.
set -e
cd "$(dirname "$0")"

AX=$(./dockprobe | grep TRASH | sed -E 's/.*rect=\(([-0-9]+),([0-9]+) ([0-9]+)x([0-9]+)\).*/\1 \2 \3 \4/')
set -- $AX
AXX=$1; AXY=$2; AXW=$3; AXH=$4
echo "AX rect: ($AXX,$AXY) ${AXW}x${AXH}"

# Capture a generous region around it.
CX=$((AXX - 30)); CY=$((AXY - 30)); CW=$((AXW + 60)); CH=$((AXH + 60))
screencapture -o -x -R"${CX},${CY},${CW},${CH}" /tmp/dockregion.png

python3 match_icon.py "$CX" "$CY" "$AXX" "$AXY" "$AXW" "$AXH"
