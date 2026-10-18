#!/usr/bin/env sh
set -eu
python3 -m pip install --upgrade ufbt
ufbt update --channel release
ufbt
printf '\nBuild finished. Look for bruce_lite.fap in dist/\n'
