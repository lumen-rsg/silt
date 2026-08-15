cd /etc || exit 10
test -f os-release || exit 11
test -d /bin || exit 12
printf 'DASH_SCRIPT=ok\n'
