#!/bin/sh
# Builds Tsundoku. Arguments are passed on to the make of Tsundoku, e.g. ./build.sh bindcatalogs
#
# MuPDF comes from the mupdf1.28_devel package if it is installed (https://kiri.sen-labs.org/x86_64). Otherwise it
# is built once from source, taken from $MUPDF_DIR or downloaded to 3rd-party/. Libraries that exist as HaikuPorts
# packages are not built but taken from the system, see MUPDF-NOTES.md for the packages needed:
#   pkgman install harfbuzz_devel openjpeg_devel jbig2dec_devel brotli_devel libjpeg_turbo_devel freetype_devel

set -e
cd "$(dirname "$0")"

MUPDF_VERSION=1.28.5
if [ -z "$MUPDF_DIR" ] && { [ -f /system/develop/headers/mupdf1.28/mupdf/fitz.h ] \
		|| [ -f /boot/home/config/develop/headers/mupdf1.28/mupdf/fitz.h ]; }; then
	unset MUPDF_DIR
else
	[ -n "$MUPDF_DIR" ] || MUPDF_DIR="$PWD/3rd-party/mupdf-$MUPDF_VERSION-source"
	export MUPDF_DIR
fi

if [ -n "$MUPDF_DIR" ] && [ ! -f "$MUPDF_DIR/build/release/libmupdf.a" ]; then
	if [ ! -d "$MUPDF_DIR" ]; then
		mkdir -p 3rd-party
		(
			cd 3rd-party
			wget -q "https://mupdf.com/downloads/archive/mupdf-$MUPDF_VERSION-source.tar.gz"
			tar xzf "mupdf-$MUPDF_VERSION-source.tar.gz"
			rm "mupdf-$MUPDF_VERSION-source.tar.gz"
		)
	fi
	make -C "$MUPDF_DIR" -j"$(nproc)" build=release HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no \
		USE_SYSTEM_FREETYPE=yes USE_SYSTEM_HARFBUZZ=yes USE_SYSTEM_ZLIB=yes USE_SYSTEM_LIBJPEG=yes \
		USE_SYSTEM_OPENJPEG=yes USE_SYSTEM_BROTLI=yes USE_SYSTEM_JBIG2DEC=yes libs
fi

make -C tsundoku "$@"
