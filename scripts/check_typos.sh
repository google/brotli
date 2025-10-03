#!/bin/bash

HERE=`realpath $(dirname "$0")`
PROJECT_DIR=`realpath ${HERE}/..`
SRC_EXT="bazel|bzl|c|cc|cmake|gni|h|html|in|java|js|m|md|nix|py|rst|sh|ts|txt|yaml|yml"
cd "${PROJECT_DIR}"
sources=`find . -type f | sort |grep -E "\.(${SRC_EXT})$" | grep -v -E "^(./)?tests/testdata/" | grep -v -E "\.min\.js$" | grep -v -E "brotli_dictionary\.txt$"`
echo "Checking sources:"
echo "${sources}"
typos -c "${HERE}/typos.toml" ${sources}
