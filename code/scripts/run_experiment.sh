#!/bin/bash
# 入口脚本 → 委托给 run_full_experiment.sh
exec "$(dirname "$0")/run_full_experiment.sh" "$@"
