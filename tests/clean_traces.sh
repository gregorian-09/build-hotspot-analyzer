#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_ROOT="$SCRIPT_DIR/cli"

echo "Cleaning all traces folders..."

find "$TEST_ROOT" -type d -name "traces" | while read -r traces_dir; do
    if [ -d "$traces_dir" ]; then
        echo "Cleaning: $traces_dir"
        # shellcheck disable=SC2115
        rm -rf "$traces_dir"/*
    fi
done

echo "All traces folders cleaned."
