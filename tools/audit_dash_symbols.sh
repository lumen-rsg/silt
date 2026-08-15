#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 ARCHIVE EXPECTED_SYMBOLS" >&2
    exit 2
fi

archive=$1
expected=$2
temporary=$(mktemp -d)
trap 'test ! -d "${temporary}" || rm -r "${temporary}"' EXIT HUP INT TERM

nm -g --undefined-only "${archive}" \
    | awk '{ print $2 }' | sort -u > "${temporary}/undefined"
nm -g --defined-only "${archive}" \
    | awk '{ print $3 }' | sort -u > "${temporary}/defined"
comm -23 "${temporary}/undefined" "${temporary}/defined" \
    > "${temporary}/external"

diff -u "${expected}" "${temporary}/external"
