#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 || -z "$1" ]]; then
   echo "usage: install-glaze.sh <install-prefix>" >&2
   exit 2
fi

readonly version=7.5.0
readonly commit=8b60d82c66311c145c4d03be3b556b555a9cb111
readonly prefix="$1"
readonly work="${RUNNER_TEMP:?RUNNER_TEMP is required}/forge-glaze-${version}"
readonly source="$work/source"
readonly build="$work/build"

if [[ -e "$work" || -e "$prefix" ]]; then
   echo "Glaze installation paths must not exist before setup" >&2
   exit 1
fi

mkdir -p "$source"
git -C "$source" init --quiet
git -C "$source" remote add origin https://github.com/stephenberry/glaze.git
git -C "$source" fetch --quiet --depth=1 origin "$commit"
git -C "$source" checkout --quiet --detach FETCH_HEAD

if [[ "$(git -C "$source" rev-parse HEAD)" != "$commit" ]]; then
   echo "Glaze checkout does not match pinned commit $commit" >&2
   exit 1
fi

cmake -S "$source" -B "$build" -G Ninja \
   -DCMAKE_BUILD_TYPE=Release \
   -DCMAKE_INSTALL_PREFIX="$prefix" \
   -Dglaze_DEVELOPER_MODE=OFF
cmake --install "$build"
