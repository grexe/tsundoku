#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# usage: run.sh open-times.sh file...  -- how long a document takes until its page count is known (polled, never slept)
. /tmp/tojivm/common.sh
start_blanker; kill_app
for f in "$@"; do
	$APP "$f" >/dev/null 2>&1 &
	took=$(wait_pages 150)
	count=$(pages)
	echo "$(basename "$f"): $took s, $count pages" >> $OUT/times.txt
	kill_app
done
cat $OUT/times.txt
finish
