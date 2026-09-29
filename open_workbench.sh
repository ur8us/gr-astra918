#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if ! find "${repo_dir}/build/python/gnuradio/astra918" -maxdepth 1 -name 'astra918_python*.so' -print -quit 2>/dev/null | grep -q .; then
    echo "Build gr-astra918 first: cmake -S . -B build && cmake --build build" >&2
    exit 1
fi

if ! /usr/bin/python3 - "${repo_dir}/grc" <<'PY'
import os
import sys
from gnuradio import gr

paths = gr.prefs().get_string("grc", "global_blocks_path", "").split(os.pathsep)
sys.exit(0 if sys.argv[1] in paths else 1)
PY
then
    export GRC_BLOCKS_PATH="${repo_dir}/grc${GRC_BLOCKS_PATH:+:${GRC_BLOCKS_PATH}}"
fi
export PYTHONPATH="${repo_dir}/build/python${PYTHONPATH:+:${PYTHONPATH}}"
export LD_LIBRARY_PATH="${repo_dir}/build${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec gnuradio-companion "${repo_dir}/examples/source_to_null.grc"
