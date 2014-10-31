#!/usr/bin/python
"""
/*
This file is part of Bohrium and copyright (c) 2012 the Bohrium
team <http://www.bh107.org>.

Bohrium is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as
published by the Free Software Foundation, either version 3
of the License, or (at your option) any later version.

Bohrium is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the
GNU Lesser General Public License along with Bohrium.

If not, see <http://www.gnu.org/licenses/>.
*/
"""

import sys
import os
from os.path import join, expanduser, exists
import shutil
import getopt
import subprocess

OK   = '\033[92m'
FAIL = '\033[91m'
ENDC = '\033[0m'
HEAD = '\033[93m'

makecommand = "make"
makefilename = "Makefile"

install_summary = []
build_summary = []

def build(components,interpreter):
    for (name, dir, fatal) in components:
        print HEAD,"***Building %s***"%name, ENDC
        mkfile = makefilename

        if not exists(join(install_dir, dir, makefilename)) and exists(join(install_dir, dir, "Makefile")):
            mkfile = "Makefile"

        try:
            p = subprocess.Popen([makecommand, "-f", mkfile,"BH_PYTHON=%s"%interpreter], cwd=join(install_dir, dir))
            err = p.wait()
        except KeyboardInterrupt:
            p.terminate()

        if err == 0:
            print OK, "Build %s successfully."%name, ENDC
        else:
            if fatal:
                print FAIL, "A build error in %s is fatal. Exiting."%name, ENDC
                sys.exit(-1)
            else:
                print FAIL, "A build error in %s is not fatal. Continuing."%name, ENDC
        build_summary.append((name, err))

def clean(components):
    for (name, dir, fatal) in components:
        print HEAD,"***Cleaning %s***"%name, ENDC
        mkfile = makefilename

        if not exists(join(install_dir, dir, makefilename)) and exists(join(install_dir, dir, "Makefile")):
            mkfile = "Makefile"

        try:
            p = subprocess.Popen([makecommand, "-f", mkfile, "clean"], cwd=join(install_dir, dir))
            err = p.wait()
        except KeyboardInterrupt:
            p.terminate()

def install(components,prefix,interpreter):
    if not exists(join(prefix,"lib")):
        os.mkdir(join(prefix,"lib"))
    for (name, dir, fatal) in components:
        print HEAD,"***Installing %s***"%name, ENDC
        mkfile = makefilename

        if not exists(join(install_dir, dir, makefilename)) and exists(join(install_dir, dir, "Makefile")):
            mkfile = "Makefile"

        try:
            p = subprocess.Popen([makecommand, "-f", mkfile,"install","BH_PYTHON=%s"%interpreter,"INSTALLDIR=%s"%prefix], cwd=join(install_dir, dir))
            err = p.wait()
        except KeyboardInterrupt:
            p.terminate()

        if err == 0:
            print OK, "Installed %s successfully."%name, ENDC
        else:
            if fatal:
                print FAIL, "A build error in %s is fatal. Exiting."%name, ENDC
                sys.exit(-1)
            else:
                print FAIL, "A build error in %s is not fatal. Continuing."%name, ENDC
        install_summary.append((name, err))


def install_config(prefix):
    if os.geteuid() == 0:#Root user
        HOME_CONFIG = "/etc/bohrium"
    else:
        HOME_CONFIG = join(join(expanduser("~"),".bohrium"))
    if not exists(HOME_CONFIG):
        os.mkdir(HOME_CONFIG)
    dst = join(HOME_CONFIG, "config.ini")
    src = join(install_dir,"config.ini.example")
    if not exists(dst):
        src_file = open(src, "r")
        src_str = src_file.read()
        src_file.close()
        dst_str = src_str.replace("/opt/bohrium",prefix)
        if sys.platform.startswith('darwin'):
            dst_str = dst_str.replace(".so",".dylib")
        dst_file = open(dst,"w")
        dst_file.write(dst_str)
        dst_file.close()
        print "Write default config file to %s"%(dst)


if __name__ == "__main__":
    debug = False
    interactive = False
    if not sys.platform.startswith('win32') and os.geteuid() == 0:#Root user
        prefix = "/opt/bohrium"
    else:
        prefix = join(join(expanduser("~"),".local"))
    interpreter = sys.executable
    try:
        install_dir = os.path.abspath(os.path.dirname(__file__))
    except NameError:
        print "The build script cannot run interactively."
        sys.exit(-1)

    try:
        opts, args = getopt.gnu_getopt(sys.argv[1:],"d",["debug","prefix=","interactive","interpreter="])
    except getopt.GetoptError, err:
        print str(err)
        sys.exit(2)
    for o, a in opts:
        if o in ("-d","--debug"):
            debug = True
        elif o in ("--prefix"):
            prefix = a
        elif o in ("--interactive"):
            interactive = True
        elif o in ("--interpreter"):
            interpreter = a
        else:
            assert False, "unhandled option"

    if sys.platform.startswith('win32'):
        makecommand="nmake"
        makefilename="Makefile.win"
    elif sys.platform.startswith('darwin'):
        makefilename="Makefile.osx"

    if interactive:
        import readline, glob
        def complete(text, state):#For autocomplete
            return (glob.glob(text+'*')+[None])[state]
        readline.set_completer_delims(' \t\n;')
        readline.parse_and_bind("tab: complete")
        readline.set_completer(complete)

        print "Please specify the installation directory:"
        answer = raw_input("[%s] "%prefix)
        if answer != "":
            prefix = expanduser(answer)
    try:
        cmd = args[0]
    except IndexError:
        print "No command given"
        print ""
        print "Known commands: build, clean, install, rebuild"
        sys.exit(-1)

    components = [\
                  # ("OPCODES","core/codegen",True),\
                  # ("CORE-INIPARSER","core/iniparser",True),\
                  # ("CORE-BHIR", "core/bhir", True),\
                  # ("CORE", "core", True),\
                  # ("THIRDPARTY", "thirdparty", True),\
                  # ("VE-GPU", "ve/gpu", False),\
                  # ("VE-CPU", "ve/cpu", True),\
                  # ("VEM-NODE", "vem/node", True),\
                  # ("VEM-CLUSTER", "vem/cluster", False),\
                  # ("VEM-PROXY", "vem/proxy", False),\
                  # ("FILTER-RANGE", "filter/range", False),\
                  # ("FILTER-PPRINT", "filter/pprint", True),\
                  # ("FILTER-VALIDATE", "filter/validate", True),\
                  # ("FILTER-TRANSITIVE-REDUCTION", "filter/transitive_reduction", True),\
                  # ("EXT-METHOD-MATMUL", "extmethods/matmul", True),\
                  # ("EXT-METHOD-VISUALIZER", "extmethods/visualizer", False),\
                  # ("EXT-METHOD-FFTW", "extmethods/fftw", False),\
                  # ("BRIDGE-C++", "bridge/cpp", True),\
                  # ("BRIDGE-C", "bridge/c", True),\
                  # ("BRIDGE-BHPY", "bridge/bhpy", True),\
#                  ("BHNUMPY", "bohrium", True),\
#                  ("FILTER-POWER", "filter/power", False),\
#                  ("FILTER-FUSION", "filter/fusion", False),\
#                  ("FILTER-STREAMING", "filter/streaming", False),\
#                  ("NumCIL", "bridge/NumCIL", False)\
                  ]

    if cmd == "rebuild":
        clean(components)
    if cmd == "build" or cmd == "rebuild":
        build(components,interpreter)
        print HEAD, "Build Summary (%s):"%prefix,ENDC
        for i in build_summary:
            if i[1] == 0:
                print OK,  "\t%s: SUCCESS"%i[0], ENDC
            else:
                print FAIL, "\t%s: FAILED"%i[0], ENDC
    elif cmd == "clean":
        clean(components)
    elif cmd == "install":
        prefix = os.path.abspath(prefix)
        if exists(prefix):
            assert os.path.isdir(prefix),"The prefix points to an existing file"
        else:
            os.makedirs(prefix)
        install(components,prefix,interpreter)
        install_config(prefix);
        print HEAD, "Install Summary (%s):"%prefix,ENDC
        for i in install_summary:
            if i[1] == 0:
                print OK,  "\t%s: SUCCESS"%i[0], ENDC
            else:
                print FAIL, "\t%s: FAILED"%i[0], ENDC
    else:
        print "Unknown command: '%s'."%cmd
        print ""
        print "Known commands: build, clean, install"
