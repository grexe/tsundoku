#!/bin/sh
# Builds the Tsundoku HPKG from dist/ (run ./build.sh first).
# The package is written to the repository root; set REVISION to override the revision (default 1).
set -e
cd "$(dirname "$0")"

APP=dist/Tsundoku
RDEF=src/app/Tsundoku.rdef
[ -f "$APP" ] || { echo "$APP not found, run ./build.sh first" >&2; exit 1; }

# The package requires the mupdf1.28 package. A binary that has MuPDF built in (./build.sh without the
# mupdf1.28_devel package installed) would also need the libraries MuPDF uses, which the package does not list.
grep -q 'libmupdf1.28.so' $APP || {
	echo "$APP does not use the shared MuPDF; install mupdf1.28_devel (https://kiri.sen-labs.org/x86_64) and rebuild" >&2
	exit 1
}

# version from the app_version resource, a development build becomes a pre-release
major=$(sed -n 's/^[[:space:]]*major[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
middle=$(sed -n 's/^[[:space:]]*middle[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
minor=$(sed -n 's/^[[:space:]]*minor[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
VERSION="$major.$middle.$minor"
grep -q 'variety[[:space:]]*=[[:space:]]*B_APPV_DEVELOPMENT' $RDEF && VERSION="$VERSION~dev"
VERSION="$VERSION-${REVISION:-1}"
ARCH=$(getarch)

# The app finds the start page next to itself, so it goes into apps/Tsundoku.
STAGE=package-build
rm -rf $STAGE
mkdir -p $STAGE/apps/Tsundoku $STAGE/data/deskbar/menu/Applications
cp -a $APP dist/license $STAGE/apps/Tsundoku/
mkdir -p $STAGE/data/licenses
cp -a dist/license/AGPL-3.0 "$STAGE/data/licenses/GNU AGPL v3"
mkdir $STAGE/apps/Tsundoku/docs
cp -a dist/docs/Start.pdf $STAGE/apps/Tsundoku/docs/   # the start page, shown when no file is given
# the user guide, built from docs/guide (by CI, or build.sh there); the package is made without it if it is not there
for guide in docs/guide/build/tsundoku-guide.pdf docs/guide/build/tsundoku-guide.epub; do
	[ -f "$guide" ] && cp -a "$guide" $STAGE/apps/Tsundoku/docs/
done
ln -s ../../../../apps/Tsundoku/Tsundoku $STAGE/data/deskbar/menu/Applications/Tsundoku
sed -e "s|@VERSION@|$VERSION|g" -e "s|@ARCH@|$ARCH|g" package/PackageInfo.in > $STAGE/.PackageInfo

PACKAGE=tsundoku-$VERSION-$ARCH.hpkg
rm -f $PACKAGE
package create -C $STAGE $PACKAGE
rm -rf $STAGE
echo "created $PACKAGE"
