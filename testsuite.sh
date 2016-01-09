#!/bin/sh

MOD="" # CTRL+g
ESC="" # \e
DVTM="./dvtm"
DVTM_EDITOR="vis"
LOG="dvtm.log"
TEST_LOG="$0.log"
UTF8_TEST_URL="http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-demo.txt"

[ ! -z "$1" ] && DVTM="$1"
[ ! -x "$DVTM" ] && echo "usage: $0 path-to-dvtm-binary" && exit 1

dvtm_input() {
	printf "$1"
}

dvtm_cmd() {
	printf "${MOD}$1\n"
	sleep 1
}

sh_cmd() {
	printf "$1\n"
	sleep 1
}

test_copymode() { # requires wget, diff, vis
	local FILENAME="UTF-8-demo.txt"
	local COPY="$FILENAME.copy"
	[ ! -e "$FILENAME" ] && (wget "$UTF8_TEST_URL" -O "$FILENAME" > /dev/null 2>&1 || return 1)
	sleep 1
	sh_cmd "cat $FILENAME"
	dvtm_cmd 'e'
	dvtm_input "?UTF-8 encoded\n"
	dvtm_input '^kvG1k$'
	dvtm_input ":wq\n"
	sleep 1
	rm -f "$COPY"
	sh_cmd "vis $COPY"
	dvtm_input 'i'
	dvtm_cmd 'p'
	dvtm_input "dd:wq\n"
	while [ ! -r "$COPY" ]; do sleep 1; done;
	dvtm_input "exit\n"
	diff -u "$FILENAME" "$COPY" 1>&2
	local RESULT=$?
	rm -f "$COPY"
	return $RESULT
}

if ! which vis > /dev/null 2>&1 ; then
	echo "vis not found, skiping copymode test"
	exit 0
fi

{
	echo "Testing $DVTM" 1>&2
	$DVTM -v 1>&2
	test_copymode && echo "copymode: OK" 1>&2 || echo "copymode: FAIL" 1>&2;
} 2> "$TEST_LOG" | $DVTM -m ^g 2> $LOG

cat "$TEST_LOG" && rm "$TEST_LOG" $LOG
