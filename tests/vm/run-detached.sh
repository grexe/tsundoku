#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# (on the VM) starts a test script in the background: ssh -f with stdout and stdin closed does not hang on a GUI app
# usage: VM_RUN_ID=n run-detached.sh script.sh [args...]
cd /tmp/tojivm
export VM_RUN_ID
/tmp/tojivm/"$@" >/tmp/tojivm/out/script.log 2>&1 </dev/null &
