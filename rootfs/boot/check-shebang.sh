#!/bin/sh
printf 'D5_SCRIPT: argc=%s first=<%s> second=<%s> env=%s\n' "$#" "$1" "$2" "$D5_ENV"
exit 23
