#! /bin/sh

for BITNESS in 32 64; do

MODTEST_OVERRIDE1=/lib/modules/$MODTEST_UNAME/modules.dep
MODTEST_OVERRIDE_WITH1=tests/tmp/modules.dep
export MODTEST_OVERRIDE1 MODTEST_OVERRIDE_WITH1

MODTEST_OVERRIDE2=/lib/modules/$MODTEST_UNAME/noexport_nodep-$BITNESS.ko
MODTEST_OVERRIDE_WITH2=tests/data/$BITNESS/normal/noexport_nodep-$BITNESS.ko
export MODTEST_OVERRIDE2 MODTEST_OVERRIDE_WITH2

MODTEST_OVERRIDE3=/etc/modprobe.conf
MODTEST_OVERRIDE_WITH3=tests/tmp/modprobe.conf
export MODTEST_OVERRIDE3 MODTEST_OVERRIDE_WITH3

MODTEST_OVERRIDE4=/lib/modules/$MODTEST_UNAME/modules.dep.bin
MODTEST_OVERRIDE_WITH4=FILE-WHICH-DOES-NOT-EXIST
export MODTEST_OVERRIDE4 MODTEST_OVERRIDE_WITH4

MODTEST_OVERRIDE5=/sys/module/noexport_nodep_$BITNESS
MODTEST_OVERRIDE_WITH5=tests/tmp/sys/module/noexport_nodep_$BITNESS
export MODTEST_OVERRIDE5 MODTEST_OVERRIDE_WITH5

MODTEST_OVERRIDE6=/sys/module/noexport_nodep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH6=tests/tmp/sys/module/noexport_nodep_$BITNESS/initstate
export MODTEST_OVERRIDE6 MODTEST_OVERRIDE_WITH6

MODTEST_OVERRIDE7=/sys/module/noexport_dep_$BITNESS
MODTEST_OVERRIDE_WITH7=tests/tmp/sys/module/noexport_dep_$BITNESS
export MODTEST_OVERRIDE7 MODTEST_OVERRIDE_WITH7

MODTEST_OVERRIDE8=/sys/module/noexport_dep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH8=tests/tmp/sys/module/noexport_dep_$BITNESS/initstate
export MODTEST_OVERRIDE8 MODTEST_OVERRIDE_WITH8

MODTEST_OVERRIDE9=/sys/module/export_nodep_$BITNESS
MODTEST_OVERRIDE_WITH9=tests/tmp/sys/module/export_nodep_$BITNESS
export MODTEST_OVERRIDE9 MODTEST_OVERRIDE_WITH9

MODTEST_OVERRIDE10=/sys/module/export_nodep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH10=tests/tmp/sys/module/export_nodep_$BITNESS/initstate
export MODTEST_OVERRIDE10 MODTEST_OVERRIDE_WITH10

MODTEST_OVERRIDE11=/sys/module/export_dep_$BITNESS
MODTEST_OVERRIDE_WITH11=tests/tmp/sys/module/export_dep_$BITNESS
export MODTEST_OVERRIDE11 MODTEST_OVERRIDE_WITH11

MODTEST_OVERRIDE12=/sys/module/export_dep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH12=tests/tmp/sys/module/export_dep_$BITNESS/initstate
export MODTEST_OVERRIDE12 MODTEST_OVERRIDE_WITH12

MODTEST_OVERRIDE13=/sys/module/noexport_doubledep_$BITNESS
MODTEST_OVERRIDE_WITH13=tests/tmp/sys/module/noexport_doubledep_$BITNESS
export MODTEST_OVERRIDE13 MODTEST_OVERRIDE_WITH13

MODTEST_OVERRIDE14=/sys/module/noexport_doubledep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH14=tests/tmp/sys/module/noexport_doubledep_$BITNESS/initstate
export MODTEST_OVERRIDE14 MODTEST_OVERRIDE_WITH14

# Set up modules.dep file.
echo "# A comment" > tests/tmp/modules.dep
echo "noexport_nodep-$BITNESS.ko:" >> tests/tmp/modules.dep
echo "bogus-$BITNESS.ko:" >> tests/tmp/modules.dep

echo "install some-command ./modprobe crap && echo SUCCESS" > tests/tmp/modprobe.conf 
echo "remove some-command ./modprobe -r crap && echo SUCCESS" >> tests/tmp/modprobe.conf 
echo "alias foobar crap" >> tests/tmp/modprobe.conf 

# Now make a fake /sys/module structure for the test
mkdir -p tests/tmp/sys/module
mkdir -p tests/tmp/sys/module/noexport_nodep_$BITNESS
mkdir -p tests/tmp/sys/module/noexport_dep_$BITNESS
mkdir -p tests/tmp/sys/module/export_nodep_$BITNESS
mkdir -p tests/tmp/sys/module/export_dep_$BITNESS
mkdir -p tests/tmp/sys/module/noexport_doubledep_$BITNESS
touch tests/tmp/sys/module/noexport_nodep_$BITNESS/initstate
touch tests/tmp/sys/module/noexport_dep_$BITNESS/initstate
touch tests/tmp/sys/module/export_nodep_$BITNESS/initstate
touch tests/tmp/sys/module/export_dep_$BITNESS/initstate
touch tests/tmp/sys/module/noexport_doubledep_$BITNESS/initstate

SIZE=$(echo `wc -c < tests/data/$BITNESS/normal/noexport_nodep-$BITNESS.ko`)

# -q works as normal.
[ "`./modprobe -q noexport_nodep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE " ]

# -q on non-existent fail, quietly.
[ "`./modprobe -q crap 2>&1`" = "" ]
if ./modprobe -q crap; then exit 1; fi

# -q on alias to non-existent succeeds, quietly.
[ "`./modprobe -q foobar 2>&1`" = "" ]
if ./modprobe -q foobar; then exit 1; fi

# -q on some other problem gives errors.
[ "`./modprobe -q bogus-$BITNESS 2>&1`" != "" ]
if ./modprobe -q bogus-$BITNESS 2>/dev/null; then exit 1; fi

MODTEST_DO_SYSTEM=1
export MODTEST_DO_SYSTEM
# Normal install command will fail.
[ "`./modprobe some-command 2>&1`" = "FATAL: Module crap not found.
FATAL: Error running install command for some_command" ]
if ./modprobe some-command 2>/dev/null; then exit 1; fi

# -q doesn't cause "modprobe crap" to succeed, but is passed through install.
[ "`./modprobe -q some-command 2>&1`" = "FATAL: Error running install command for some_command" ]
if ./modprobe -q some-command 2>/dev/null; then exit 1; fi

## Remove
# All in proc
cat > tests/tmp/proc <<EOF
noexport_nodep_$BITNESS 100 0 -
EOF

# -q works as normal.
[ "`./modprobe -r -q noexport_nodep-$BITNESS 2>&1`" = "DELETE_MODULE: noexport_nodep_$BITNESS EXCL " ]

# -q on non-existent module fails, silently.
[ "`./modprobe -r -q crap 2>&1`" = "" ]
if ./modprobe -r -q crap; then exit 1; fi

MODTEST_DO_SYSTEM=1
export MODTEST_DO_SYSTEM
# Normal remove command will fail.
[ "`./modprobe -r some-command 2>&1`" = "FATAL: Module crap not found.
FATAL: Error running remove command for some_command" ]
if ./modprobe -r some-command 2>/dev/null; then exit 1; fi

# -q doesn't cause "modprobe -r crap" to succeed, but silences it.
[ "`./modprobe -r -q some-command 2>&1`" = "FATAL: Error running remove command for some_command" ]
if ./modprobe -r -q some-command 2>/dev/null; then exit 1; fi

done
