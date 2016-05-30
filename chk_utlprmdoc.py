#!/usr/bin/env python
# -*- coding: utf-8 -*-
from optparse import OptionParser
import sqlite3
import os
from subprocess import call
import sys

def chk_in_xml(pathname, outname):
    # データベースへ接続
    conn = sqlite3.connect(os.path.join(pathname,'edprm.db'))
    err_skip = ()
    # カーソルの作成
    cur = conn.cursor()    
    cur.execute("""SELECT id FROM _edprm_master""");
    all=0;
    err=0;
    for id in cur.fetchall():
        if id[0] not in err_skip:
            all += 1
            try:
                retcode = call("utlprm2docxml" + " -i " + '"' + id[0]  + '"' +" -o " + outname, shell=True)
                if abs(retcode) > 0:
                    print  id[0], " out error ", retcode
                    err += 1
                else:
                    try:
                        lint = call("xmllint --format "+outname+" > /dev/null", shell=True)
                        if lint != 0:
                            print  id[0], " xmllint error", lint
                            err += 1
                        else:
                            print id[0], " xmllint return ", lint
                    except OSError, e:
                        print >>sys.stderr, "err:", e
                        print  id[0], " xmllint excp 99"
                        err += 1
            except OSError, e:
                print >>sys.stderr, "err:", e
                err += 1
        else:
            print id[0], " is skiped!"

    try:
        retcode = call("utlprm2docxml" + " -g gao -lang 0 -datefmt 3" + " -o group.xml", shell=True)
        if abs(retcode) > 0:
            print  "group out error ", retcode
            err += 1
        else:
            try:
                lint = call("xmllint --format group.xml  > /dev/null", shell=True)
                if lint != 0:
                    print  "group.xml xmllint error", lint
                    err += 1
                else:
                    print "group.xml xmllint return ", lint
                    all += 1
            except OSError, e:
                    print >>sys.stderr, "err:", e
                    print  "group.xml xmllint excp"
                    err += 1
    except OSError, e:
        print >>sys.stderr, "err:", e
        print "group utlprm2docxml excp"
        err += 1

    print "ALL: ", all, " Error: ", err, " Skip: ", len(err_skip)

if __name__ == "__main__":
    parser = OptionParser()
    parser.add_option("-p", "--path", dest="pathname", default=os.environ['DMPATH'])
    parser.add_option("-o", "--outname", dest="outname", default='in.xml')
    (options, args) = parser.parse_args()
    chk_in_xml(options.pathname, options.outname)
