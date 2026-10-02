#!/bin/sh
# Builds MuPDF (once) and Tsundoku. Arguments are passed on to the make of Tsundoku, e.g. ./build.sh bindcatalogs
#
# MuPDF is taken from $MUPDF_DIR, or downloaded to 3rd-party/. Libraries that exist as HaikuPorts packages are
# not built but taken from the system, see PLAN-mupdf.md for the packages needed:
#   pkgman install harfbuzz_devel openjpeg_devel jbig2dec_devel brotli_devel libjpeg_turbo_devel freetype_devel

set -e
cd "$(dirname "$0")"

MUPDF_VERSION=1.28.5
[ -n "$MUPDF_DIR" ] || MUPDF_DIR="$PWD/3rd-party/mupdf-$MUPDF_VERSION-source"
export MUPDF_DIR

if [ ! -f "$MUPDF_DIR/build/release/libmupdf.a" ]; then
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
