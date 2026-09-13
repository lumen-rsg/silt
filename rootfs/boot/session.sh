#!/bin/sh
# The supervisor owns service startup. This script initializes the user session.
. /etc/profile || exit 1
printf 'SILT_SESSION_READY: sh startup complete\n'
exec /bin/sh -i
