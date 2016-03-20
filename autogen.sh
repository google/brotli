# !/bin/sh -e

automake -a

AUTORECONF=`which autoreconf 2>/dev/null`
if test $? -ne 0; then
  echo "No 'autoreconf' found. You must install the autoconf package."
  exit 1
fi

# create m4 before autoreconf
mkdir m4 2>/dev/null

$AUTORECONF --install --force --symlink || exit $?

echo
echo "----------------------------------------------------------------"
echo "Initialized build system. For a common configuration please run:"
echo "----------------------------------------------------------------"
echo
echo "./configure"
echo
