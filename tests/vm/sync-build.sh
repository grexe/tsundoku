#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Gregor B. Rosenauer & Claude
# usage: tests/vm/sync-build.sh  (on the Mac)
# Copies the sources to the clone in the VM, builds there with the test hooks and prints the errors and the exit line.
# Needs docs/guide/build (the guide that CI builds, or ./docs/guide/build.sh) for the start page next to the program.
# The VM: TOJI_VM_PORT (2222), TOJI_VM_USER (user), TOJI_VM_HOST (localhost), TOJI_VM_DIR (the clone there, ~/Develop/toji).
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PORT=${TOJI_VM_PORT:-2222}; TARGET=${TOJI_VM_USER:-user}@${TOJI_VM_HOST:-localhost}; DIR=${TOJI_VM_DIR:-Develop/toji}
SSH="ssh -o ConnectTimeout=20 -o ServerAliveInterval=5 -o ServerAliveCountMax=3 -p $PORT $TARGET"
$SSH 'touch /tmp/ts_stamp' </dev/null
for try in 1 2 3 4 5 6; do
	COPYFILE_DISABLE=1 tar -cf - -C "$ROOT" src/Makefile lib src sounds build.sh package tests/ScriptingTest.cpp tests/vm images/toji-logo.png 2>/dev/null \
		| $SSH "tar -C ~/$DIR -xf - 2>&1 | grep -v 'in the future\|LIBARCHIVE'"
	[ "${PIPESTATUS[1]}" = "0" ] && break
	sleep 6
done
# what was extracted with a time in the future (the clock of the VM can be behind) is touched, so that it counts as changed
$SSH "find ~/$DIR/src ~/$DIR/lib -newer /tmp/ts_stamp -type f -exec touch {} +" </dev/null
if [ -f "$ROOT/docs/guide/build/toji-guide.pdf" ]; then
	$SSH "mkdir -p ~/$DIR/docs/guide/build" </dev/null
	scp -q -P $PORT "$ROOT"/docs/guide/build/toji-guide.pdf "$ROOT"/docs/guide/build/toji-start.pdf $TARGET:$DIR/docs/guide/build/ </dev/null
fi
$SSH 'rm -f /tmp/ts_build.log' </dev/null
$SSH "chmod +x ~/$DIR/tests/vm/*.sh" </dev/null
ssh -f -p $PORT $TARGET "~/$DIR/tests/vm/vm-build.sh >/dev/null 2>&1 </dev/null &" </dev/null
sleep 20
i=0
while [ $i -lt 120 ]; do
	$SSH 'tail -3 /tmp/ts_build.log 2>/dev/null | grep -q "^exit"' </dev/null 2>/dev/null && break
	i=$((i + 1)); sleep 10
done
$SSH 'grep -n "error\|^exit" /tmp/ts_build.log | grep -v UNUSED | head' </dev/null
