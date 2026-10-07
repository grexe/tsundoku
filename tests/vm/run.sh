#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# usage: tests/vm/run.sh script.sh [args...]   (on the Mac)
# Copies this folder to the VM, runs the script there detached (a GUI app started over ssh would hang the connection),
# waits for it by polling, and fetches what it left (screenshots, logs) into tests/vm/out/.
# The VM: TOJI_VM_PORT (2222), TOJI_VM_USER (user), TOJI_VM_HOST (localhost); the timeout in seconds: TOJI_VM_TIMEOUT (600).
cd "$(dirname "$0")" || exit 1
PORT=${TOJI_VM_PORT:-2222}; TARGET=${TOJI_VM_USER:-user}@${TOJI_VM_HOST:-localhost}
SSH="ssh -o ConnectTimeout=20 -o ServerAliveInterval=5 -o ServerAliveCountMax=3 -p $PORT $TARGET"
script=$1; shift
[ -f "$script" ] || { echo "usage: $0 script.sh [args...]" >&2; exit 1; }
RUN=$(date +%s)
$SSH 'rm -rf /tmp/tojivm/out; mkdir -p /tmp/tojivm/out' </dev/null
scp -q -P $PORT common.sh *.sh $TARGET:/tmp/tojivm/ </dev/null || exit 1
args=""
for a in "$@"; do args="$args '$a'"; done
$SSH "chmod +x /tmp/tojivm/*.sh; VM_RUN_ID=$RUN /tmp/tojivm/run-detached.sh $script$args" </dev/null
i=0
while [ $i -lt $(( ${TOJI_VM_TIMEOUT:-600} / 5 )) ]; do
	$SSH "test -f /tmp/tojivm/done.$RUN" </dev/null 2>/dev/null && break
	i=$((i + 1)); sleep 5
done
rm -rf out; mkdir -p out
scp -q -r -P $PORT "$TARGET:/tmp/tojivm/out/*" out/ </dev/null 2>/dev/null
[ $i -ge $(( ${TOJI_VM_TIMEOUT:-600} / 5 )) ] && echo "timeout: the script did not finish" >&2
echo "results in $(pwd)/out:"; ls out
