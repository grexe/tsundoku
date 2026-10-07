#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# (on the VM, started by sync-build.sh) the test build of Toji (with the test hooks); log in /tmp/ts_build.log, last line "exit N"
. ~/config/settings/profile >/dev/null 2>&1
cd ~/Develop/toji
# the clock of the VM can be behind (after a restart): objects can look newer than their sources and make builds nothing.
# All objects are set old and the binary is removed (ccache keeps the rebuild quick)
find src/objects.* -type f -exec touch -t 202001010000 {} + 2>/dev/null
rm -f dist/Toji
touch src/ui/PDFView.cpp src/ui/PDFWindow.cpp
./build.sh -k DEFINES=TOJI_TESTING > /tmp/ts_build.log 2>&1
echo "exit $?" >> /tmp/ts_build.log
