#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# usage: run.sh shots.sh file[:page]...  -- a screenshot of each document (at the page, 1 by default), in the frame at the top left
. /tmp/tojivm/common.sh
start_blanker; kill_app
for spec in "$@"; do
	f=${spec%:*}; page=${spec##*:}; [ "$page" = "$spec" ] && page=1
	start_app "$f"
	[ "$page" != 1 ] && { hey Toji do Goto of Document of Window 0 with page=$page >/dev/null 2>&1; sleep 3; }
	shot "$(basename "$f")-$page"
	kill_app
done
finish
