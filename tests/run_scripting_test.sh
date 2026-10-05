#!/bin/sh
# Runs ScriptingTest against a Toji that has a PDF open. On Haiku: tests/run_scripting_test.sh <file.pdf>
cd "$(dirname "$0")"
g++ -o /tmp/ScriptingTest ScriptingTest.cpp -lbe || exit 1
Toji "$1" &
sleep 12
/tmp/ScriptingTest
result=$?
hey Toji quit >/dev/null 2>&1
exit $result
