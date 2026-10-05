#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
#
# Checks the rule of lib/: the files there are MIT, and use nothing that is copyleft (no header of src/, MuPDF or DjVuLibre).
cd "$(dirname "$0")/.." || exit 2
failed=0
fail() { echo "FAIL: $*"; failed=1; }

for f in lib/*.cpp lib/*.h; do
	grep -q "SPDX-License-Identifier: MIT" "$f" || fail "$f has no MIT identifier"
	grep -qi "AGPL\|GNU General Public" "$f" && fail "$f mentions a copyleft license"
	# local includes must be files of lib/
	for inc in $(grep -o '^#include "[^"]*"' "$f" | sed 's/#include "\(.*\)"/\1/'); do
		[ -f "lib/$inc" ] || fail "$f includes \"$inc\", which is not in lib/"
	done
	grep -n '^#include <\(mupdf\|libdjvu\|djvu\|ddjvuapi\|jbig2\)' "$f" && fail "$f includes a copyleft library"
done

# nothing outside lib/ may claim to be MIT
for f in $(git ls-files src | grep -E '\.(cpp|h)$'); do
	grep -q "SPDX-License-Identifier: MIT" "$f" && fail "$f is MIT but is not in lib/"
done

[ $failed = 0 ] && echo "lib/ is clean: MIT files that need nothing copyleft"
exit $failed
