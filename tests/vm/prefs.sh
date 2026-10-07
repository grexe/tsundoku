#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# usage: run.sh prefs.sh file [page]  -- the preferences window (page of the settings in TOJI_PREFS_PAGE: 1 = Display)
. /tmp/tojivm/common.sh
start_blanker; kill_app; settings_aside
TOJI_PREFS_PAGE=${2:-1} start_app "$1"
tstx do_preferences; sleep 3; shot prefs
kill_app; settings_back
finish
