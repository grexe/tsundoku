#!/bin/sh
# Builds the Tsundoku HPKG from dist/ (run ./build.sh first).
# The package is written to the repository root; set REVISION to override the revision (default 1).
set -e
cd "$(dirname "$0")"

APP=dist/Tsundoku
RDEF=tsundoku/beos/Tsundoku.rdef
[ -f "$APP" ] || { echo "$APP not found, run ./build.sh first" >&2; exit 1; }

# version from the app_version resource, a development build becomes a pre-release
major=$(sed -n 's/^[[:space:]]*major[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
middle=$(sed -n 's/^[[:space:]]*middle[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
minor=$(sed -n 's/^[[:space:]]*minor[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
VERSION="$major.$middle.$minor"
grep -q 'variety[[:space:]]*=[[:space:]]*B_APPV_DEVELOPMENT' $RDEF && VERSION="$VERSION~dev"
VERSION="$VERSION-${REVISION:-1}"
ARCH=$(getarch)

# The app finds docs, fonts and encodings next to itself, so they all go into apps/Tsundoku.
STAGE=package-build
rm -rf $STAGE
mkdir -p $STAGE/apps/Tsundoku $STAGE/data/deskbar/menu/Applications
cp -a $APP dist/fonts dist/encodings dist/license $STAGE/apps/Tsundoku/
mkdir $STAGE/apps/Tsundoku/docs
cp -a dist/docs/Start.pdf $STAGE/apps/Tsundoku/docs/   # the start page, shown when no file is given
ln -s ../../../../apps/Tsundoku/Tsundoku $STAGE/data/deskbar/menu/Applications/Tsundoku
sed -e "s|@VERSION@|$VERSION|g" -e "s|@ARCH@|$ARCH|g" package/PackageInfo.in > $STAGE/.PackageInfo

PACKAGE=tsundoku-$VERSION-$ARCH.hpkg
rm -f $PACKAGE
package create -C $STAGE $PACKAGE
rm -rf $STAGE
echo "created $PACKAGE"
