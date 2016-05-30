#!/bin/sh
function main () {
	if [ 2 -ne $# ] ; then
		cat << _EOF
Checking ip contact...Not OK
Cannot connect to MO service, exiting...
_EOF
		return 1
	fi

	local bsname=$1
	local line=
	local classname=

	local tm=`date +"%Y-%m-%d %a %H:%M:%S"`

	case $2 in
	INIT-SYS*)
	cat << _EOF
  $bsname ${tm}
  M0133 RESET THE SYSTEM : COMPLD
  RESULT : OK
  ;
ossuser$
_EOF
	sleep 6
		;;
	INIT-RRH:CONN_PORT_ID=0*)
	cat << _EOF
  $bsname $tm
  M0143 RESET THE SYSTEM : COMPLD
  RESULT : OK
  ;
ossuser$
_EOF
	sleep 3
		;;
	INIT-RRH:CONN_PORT_ID=1*)
	cat << _EOF
  $bsname $tm
  M0153 RESET THE SYSTEM : COMPLD
  RESULT : OK
  ;
ossuser$
_EOF
sleep 3
		;;
	INIT-RRH:CONN_PORT_ID=2*)
	cat << _EOF
  $bsname $tm
  M0163 RESET THE SYSTEM : COMPLD
  RESULT : OK
  ;
ossuser$
_EOF
sleep 3
		;;
	INIT-RRH:CONN_PORT_ID=4*)
	cat << _EOF
  $bsname $tm
  M0173 RESET THE SYSTEM : COMPLD
  RESULT : OK
  ;
ossuser$
_EOF
sleep 3
		;;
	*)
	cat << _EOF
  $bsname $tm
  M0203 RESET THE SYSTEM : NOT COMPLD
  RESULT : ERROR
  ;
ossuser$
_EOF
sleep 3
		;;
	esac
}

main $@
