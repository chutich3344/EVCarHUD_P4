#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
cc -Wall -Wextra -DXADAS_NO_UI -I../../main test.c ../../main/xadas.c -o /tmp/xadas_test && /tmp/xadas_test
