#!/bin/sh
set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=0.5.13.5
archive_name=dash-${version}.tar.gz
source_url=http://gondor.apana.org.au/~herbert/dash/files/${archive_name}
expected_sha=40090101a2a491f13e901d3d48e90414f26634628b9bfff35ff540363c227a7d
prepared_marker=${expected_sha}:patchset-4
cache_dir=${project_root}/subprojects/packagecache
archive=${cache_dir}/${archive_name}
target=${project_root}/vendor/dash-${version}

mkdir -p "${cache_dir}"
mkdir -p "${project_root}/vendor"

if [ ! -f "${archive}" ]; then
    curl -fL "${source_url}" -o "${archive}.part"
    mv "${archive}.part" "${archive}"
fi

actual_sha=$(sha256sum "${archive}" | awk '{ print $1 }')
if [ "${actual_sha}" != "${expected_sha}" ]; then
    echo "dash archive checksum mismatch" >&2
    echo "expected: ${expected_sha}" >&2
    echo "actual:   ${actual_sha}" >&2
    exit 1
fi

if [ -f "${target}/.silt-prepared" ] \
    && [ "$(cat "${target}/.silt-prepared")" = "${prepared_marker}" ]; then
    exit 0
fi

if [ -e "${target}" ]; then
    echo "refusing to replace incomplete dash source tree: ${target}" >&2
    exit 1
fi

temporary=$(mktemp -d "${project_root}/subprojects/.dash-${version}.XXXXXX")
trap 'test ! -d "${temporary}" || rm -r "${temporary}"' EXIT HUP INT TERM

tar -xzf "${archive}" --strip-components=1 -C "${temporary}"
patch -d "${temporary}" -p1 \
    < "${project_root}/ports/dash/patches/0001-avoid-floating-point-in-size-bound.patch"
patch -d "${temporary}" -p1 \
    < "${project_root}/ports/dash/patches/0002-gate-floating-point-printf.patch"
patch -d "${temporary}" -p1 \
    < "${project_root}/ports/dash/patches/0003-avoid-glob-flag-redefinition.patch"
(
    cd "${temporary}"
    ./configure --without-libedit --disable-tee --disable-memfd-create
    make -j4
    cc -E -x c \
        -include "${project_root}/ports/dash/config.silt.h" \
        -o src/builtins.def src/builtins.def.in
    (cd src && sh ./mkbuiltins builtins.def)
)

printf '%s\n' "${prepared_marker}" > "${temporary}/.silt-prepared"
mv "${temporary}" "${target}"
trap - EXIT HUP INT TERM

echo "prepared dash ${version} in ${target}"
