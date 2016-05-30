#!/bin/sh

w_lm=`basename $0`

#/usr/bin/valgrind  --leak-check=full --track-fds=yes --trace-children=yes --time-stamp=yes --log-file=/tmp/${w_lm} ${w_lm}_org "$@"
#strace /usr/bin/valgrind  --leak-check=full --track-fds=yes --trace-children=no --time-stamp=yes --log-file=/tmp/${w_lm}_`date +%Y%m%d%H%M%S` ${0}_org "$@" > /tmp/${w_lm}_valgrind 2>&1
/usr/bin/valgrind  --leak-check=full --track-fds=yes --trace-children=no --time-stamp=yes --log-file=/tmp/${w_lm}_`date +%Y%m%d%H%M%S` ${0}_dbg "$@"
