#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# usage: run.sh pageturn.sh file [page]  -- frames of the page turn (fancy mode) at fixed progress, single and double-sided flow
. /tmp/tojivm/common.sh
start_blanker; kill_app; settings_aside
start_app "$1" ${2:-9}
tstx do_flowsingle; sleep 2; tstx do_fitpage; sleep 3
for t in 0.3 0.6 0.85; do tstx turn t=$t; sleep 3; shot single_$t; tstx turnend; sleep 2; done
tstx do_flowdouble; sleep 3
tstx turn t=0.5; sleep 3; shot double_0.5; tstx turnend
kill_app; settings_back
finish
