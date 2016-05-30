 #!/usr/bin/python
import sys
import csv
import re

def parse_hex(x):
    if x == "NV":
        return None
    return int(x, 16)

def parse_int(x):
    return int(x)

def parse_str(x):
    if type(x) != type(""):
        raise Exception()
    return x

def parse_symbol(x):
    parse_str(x)
    if check_symbol(x) != True:
        y = fix_symbol(x)
        print "warning: '%s' is not a valid symbol name, changed to '%s'" % (x, y)
        return y
    return x

def parse_none(x):
#    if x != "":
#        raise Exception("expected empty got <%s>" % x)
    return None

def check_symbol(x):
    m_obj = re.match('^[a-zA-Z]+[a-zA-Z_0-9]*$', x)
    if m_obj:
        return True
    return False

def fix_symbol(x):
    y = re.sub('[^a-zA-Z_0-9]', "_", x)
    return y

parsers = {"parser10":
    [
     ["num"        ,parse_int, 'No'],
     ["regname"    ,parse_symbol, 'Register Name'],
     ["name"       ,parse_symbol, 'Bit Field Name'],
     ["offset"     ,parse_hex, 'Address'],
     ["msb"        ,parse_int, 'MSB Bit'],
     ["width"      ,parse_int, 'Bit\nWidth'],
     ["init_val"   ,parse_hex, 'Initial\ndefault\nvalue\n(HEX)'],
     ["sreset_val" ,parse_hex, 'SFT\nReset\ndefault\nvalue\n(HEX)'],
     ["hreset_val" ,parse_hex, 'HW\nReset\ndefault\nvalue\n(HEX)'],
     ["type"       ,parse_str, 'Type\n(See "Type" sheet)']],
"parser11":
    [
     ["ignore"     ,parse_none,''],
     ["num"        ,parse_int, 'No'],
     ["name"       ,parse_symbol, 'Register Name'],
     ["offset"     ,parse_hex, 'Address'],
     ["msb"        ,parse_int, 'MSB Bit'],
     ["width"      ,parse_int, 'Bit\nWidth'],
     ["init_val"   ,parse_hex, 'Initial\ndefault\nvalue\n(HEX)'],
     ["sreset_val" ,parse_hex, 'SFT\nReset\ndefault\nvalue\n(HEX)'],
     ["hreset_val" ,parse_hex, 'HW\nReset\ndefault\nvalue\n(HEX)'],
     ["type"       ,parse_str, 'Type\n(See "Type" sheet)'],
     ["remark"     ,parse_str, 'Remarks']],
"parser12":
    [["ignore"     ,parse_none,''],
     ["num"        ,parse_int, 'No'],
     ["ignore"     ,parse_none,'Table Name'],
     ["name"       ,parse_symbol, 'Register Name'],
     ["offset"     ,parse_hex, 'Address'],
     ["msb"        ,parse_int, 'MSB Bit'],
     ["width"      ,parse_int, 'Bit\nWidth'],
     ["init_val"   ,parse_hex, 'Initial\ndefault\nvalue\n(HEX)'],
     ["sreset_val" ,parse_hex, 'SFT\nReset\ndefault\nvalue\n(HEX)'],
     ["hreset_val" ,parse_hex, 'HW\nReset\ndefault\nvalue\n(HEX)'],
     ["type"       ,parse_str, 'Type\n(See "Type" sheet)'],
     ["remark"     ,parse_str, 'Remarks']],
           }


class BitField(object):
    def __init__(self):
        self.skip = False

    def short(self):
        return "reg:% -10ss field:% -14s: (%d:%d) - 0x%08x" % (self.regname, self.name, self.msb, self.width, self.init_val)

    def start_bit(self):
        return self.msb

    def stop_bit(self):
        return self.msb - (self.width - 1)

    def mask(self):
        return ((1<<(self.width)) - 1) << (self.stop_bit())

    def dml(self, regname):
        if self.type == "RW":
            template = ""
        else:
            template = "is (%s)" % self.type

        r = "    field %s [%d:%d] %s \"%s\";\n" % (self.name, self.start_bit(), self.stop_bit(), template, "%s field description here" % self.name)

        return r

    def parse(self, row, p):
                        # FIXME: very simple parser for now
        for (col, (member, parse_func, header)) in enumerate(p):
            self.__dict__[member] = parse_func(row[col])
            continue

class Register(object):
    def __init__(self, offset, name):
        self.fields = []
        self.offset = offset
        self.name = name
    def validate(self):
        mask = 0x0
#        print "Validate %08x" % self.offset
        for f in self.fields:
#            print "field: %s" % f.short()
#            print "Adding %s %s %s (msb:%d width:%d)" % (bin(f.mask()), bin(mask),f.name, f.msb, f.width)

            if (mask & f.mask()) != 0:
#                print "collision detetected in <%s> " % f.name
                f.skip = True
            mask |= f.mask()

def main(csvfile, dmlfile, base_address, continue_on_error, parsername, start_row):
    f = file(csvfile, "rb")
    reader = csv.reader(f, delimiter=',', quotechar='"')

    regs = {}
    current_row = 0
    parser = parsers[parsername]
    num_fields = 0
    for row in reader:

        current_row += 1
        if current_row < start_row:
            continue

        if len(row) != len(parser):
            if not continue_on_error:
                sys.stderr.write("Failed to parse row %d<%r>\n"
                                 % (current_row, row))
                raise Exception("Parse error row %d" % current_row)
        else:
            b = BitField()
            try:
                b.parse(row, parser)
                if (b.offset & base_address) != base_address:
                    raise Exception("Illegal address %08x for base_address %08x"
                                    % (b.offset, base_address))
                b.offset -= base_address

            except ValueError, e:
                sys.stderr.write("Failed to parse <%r>\n" % row)
                if not continue_on_error:
                    raise e
                else:
                    continue

            try:
                r = regs[b.offset]
            except KeyError:
                # first time we see this offset -> new register
                r = Register(b.offset, b.regname)
                regs[b.offset] = r

            r.fields.append(b)
            num_fields += 1
#            print b.short()

    print "Found %d registers / %d fields in %s." % (len(regs), num_fields, csvfile)
    if len(regs) == 0:
        print " Try another parser with the --parser option."

    skipped = 0
    if dmlfile == "-":
        dmlf = sys.stdout
    else:
        dmlf = file(dmlfile, "w")
    dmlf.write("""
/* This file is automatically generated by the csv2dml.py script.
 * Please consider regenerating it from the .csv file instead of
 * manually changing.
 */

dml 1.2;
bank regs {
""")
    for offset in sorted(regs):
        r = regs[offset]
        dmlf.write("    register %s @ 0x%08x {\n" % (r.fields[0].regname, offset))
        r.validate()
        for f in r.fields:
            if not f.skip:
                dmlf.write("        " + f.dml(r.name))
            else:
                skipped += 1
        dmlf.write("    }\n")

    # add our own soft/hard reset
    dmlf.write("""
    method soft_reset() {
        log "info", 2: "do_sreset";
        call $do_sreset;
    }
    method hard_reset() {
        log "info", 2: "do_hreset";
        call $do_hreset;
    }
""")
    # close the bank
    dmlf.write("}\n")

    # add post_init() to handle PON values for the device.
    dmlf.write("""
/* Provide a default post_init_extra() so it compiles
 * without requiring the specific devices to implement it.
 */
method post_init_extra() default { }

method post_init() {
    call $post_init_extra;
    log "info", 2: "post_init called.";
    if (!SIM_object_is_configured($obj) && !SIM_is_restoring_state($obj)) {
        /* Only call do_PON on object creation. For all other occasions
         * do_hreset/do_sreset should be called.
         */
        log "info", 2: "post_init calling do_PON.";
        call $do_PON;
        call $do_PON_extra;
    }
}
""")

    if skipped != 0:
        print "Skipped %d overlapping fields." % skipped

    def gen_reset_function(dmlf, name, member, regs):
        dmlf.write("method %s() {\n" % name)
        for offset in sorted(regs):
            r = regs[offset]
            r.validate()
            for f in r.fields:
                val = f.__dict__[member]
                if not f.skip and val != None:
                    #if member == "init_val" and val == 0:
                        # optimise, don't empty $reg.field=0 for PON
                        # since that is the default and quite common.
                        #continue
                    dmlf.write("    $regs.%s.%s = 0x%08x;\n" % (
                                r.name, f.name, val))
        dmlf.write("}\n\n")
    gen_reset_function(dmlf, "do_PON", "init_val", regs)
    gen_reset_function(dmlf, "do_hreset", "hreset_val", regs)
    gen_reset_function(dmlf, "do_sreset", "sreset_val", regs)

import argparse

class ParseHex(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
#        print('FOO <%r> <%r> <%r>' % (namespace, self.dest, values ))
        setattr(namespace, self.dest, int(values, 16))


if __name__ == "__main__":


    parser = argparse.ArgumentParser(description='Sample description.')

    parser.add_argument('--csvfile',
                        dest='csvfile',
                        required = True,
                        help="hello")

    parser.add_argument('--dmlfile',
                        dest='dmlfile',
                        required = True)

    parser.add_argument('--parser',
                        dest='parser',
                        default = "parser10",
                        help = "Set to \"show\" to display avialable parsers.")

    parser.add_argument('--start-row',
                        dest='start_row',
                        default = 3,
                        type = int,
                        help = "Start row.")

    parser.add_argument('--base-address',
                        action = ParseHex,
                        default = 0,
                        help = "Base address in hex to subtract.")

    parser.add_argument('--continue-on-parse-error',
                        action = "store_true")

    args = parser.parse_args()

    if args.parser == "show":
        for p in parsers:
            print
            print "--parser=%s" % p
            for (i, x) in enumerate(parsers[p]):
                print "  col:%d - %s" % (i, x[0])
        sys.exit(0)

    main(args.csvfile,
         args.dmlfile,
         args.base_address,
         args.continue_on_parse_error,
         args.parser,
         args.start_row)
