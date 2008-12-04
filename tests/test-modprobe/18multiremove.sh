#! /bin/sh

# modprobe -r supports multiple module names now.

for BITNESS in 32 64; do

# Inputs
MODTEST_OVERRIDE1=/lib/modules/$MODTEST_UNAME
MODTEST_OVERRIDE_WITH1=tests/data/$BITNESS/normal
export MODTEST_OVERRIDE1 MODTEST_OVERRIDE_WITH1

MODTEST_OVERRIDE2=/lib/modules/$MODTEST_UNAME/export_dep-$BITNESS.ko
MODTEST_OVERRIDE_WITH2=tests/data/$BITNESS/normal/export_dep-$BITNESS.ko
export MODTEST_OVERRIDE2 MODTEST_OVERRIDE_WITH2

MODTEST_OVERRIDE3=/lib/modules/$MODTEST_UNAME/noexport_dep-$BITNESS.ko
MODTEST_OVERRIDE_WITH3=tests/data/$BITNESS/normal/noexport_dep-$BITNESS.ko
export MODTEST_OVERRIDE3 MODTEST_OVERRIDE_WITH3

MODTEST_OVERRIDE4=/lib/modules/$MODTEST_UNAME/noexport_nodep-$BITNESS.ko
MODTEST_OVERRIDE_WITH4=tests/data/$BITNESS/normal/noexport_nodep-$BITNESS.ko
export MODTEST_OVERRIDE4 MODTEST_OVERRIDE_WITH4

MODTEST_OVERRIDE5=/lib/modules/$MODTEST_UNAME/export_nodep-$BITNESS.ko
MODTEST_OVERRIDE_WITH5=tests/data/$BITNESS/normal/export_nodep-$BITNESS.ko
export MODTEST_OVERRIDE5 MODTEST_OVERRIDE_WITH5

MODTEST_OVERRIDE6=/lib/modules/$MODTEST_UNAME/noexport_doubledep-$BITNESS.ko
MODTEST_OVERRIDE_WITH6=tests/data/$BITNESS/normal/noexport_doubledep-$BITNESS.ko
export MODTEST_OVERRIDE6 MODTEST_OVERRIDE_WITH6

MODTEST_OVERRIDE7=/lib/modules/$MODTEST_UNAME/modules.dep
MODTEST_OVERRIDE_WITH7=tests/tmp/modules.dep
export MODTEST_OVERRIDE7 MODTEST_OVERRIDE_WITH7

MODTEST_OVERRIDE8=/etc/modprobe.conf
MODTEST_OVERRIDE_WITH8=FILE-WHICH-DOESNT-EXIST
export MODTEST_OVERRIDE8 MODTEST_OVERRIDE_WITH8

MODTEST_OVERRIDE9=/lib/modules/$MODTEST_UNAME/modules.dep.bin
MODTEST_OVERRIDE_WITH9=FILE-WHICH-DOES-NOT-EXIST
export MODTEST_OVERRIDE9 MODTEST_OVERRIDE_WITH9

MODTEST_OVERRIDE10=/proc/modules
MODTEST_OVERRIDE_WITH10=tests/tmp/proc
export MODTEST_OVERRIDE10 MODTEST_OVERRIDE_WITH10

MODTEST_OVERRIDE11=/sys/module/noexport_nodep_$BITNESS
MODTEST_OVERRIDE_WITH11=tests/tmp/sys/module/noexport_nodep_$BITNESS
export MODTEST_OVERRIDE11 MODTEST_OVERRIDE_WITH11

MODTEST_OVERRIDE12=/sys/module/noexport_nodep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH12=tests/tmp/sys/module/noexport_nodep_$BITNESS/initstate
export MODTEST_OVERRIDE12 MODTEST_OVERRIDE_WITH12

MODTEST_OVERRIDE13=/sys/module/noexport_dep_$BITNESS
MODTEST_OVERRIDE_WITH13=tests/tmp/sys/module/noexport_dep_$BITNESS
export MODTEST_OVERRIDE13 MODTEST_OVERRIDE_WITH13

MODTEST_OVERRIDE14=/sys/module/noexport_dep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH14=tests/tmp/sys/module/noexport_dep_$BITNESS/initstate
export MODTEST_OVERRIDE14 MODTEST_OVERRIDE_WITH14

MODTEST_OVERRIDE15=/sys/module/export_nodep_$BITNESS
MODTEST_OVERRIDE_WITH15=tests/tmp/sys/module/export_nodep_$BITNESS
export MODTEST_OVERRIDE15 MODTEST_OVERRIDE_WITH15

MODTEST_OVERRIDE16=/sys/module/export_nodep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH16=tests/tmp/sys/module/export_nodep_$BITNESS/initstate
export MODTEST_OVERRIDE16 MODTEST_OVERRIDE_WITH16

MODTEST_OVERRIDE17=/sys/module/export_dep_$BITNESS
MODTEST_OVERRIDE_WITH17=tests/tmp/sys/module/export_dep_$BITNESS
export MODTEST_OVERRIDE17 MODTEST_OVERRIDE_WITH17

MODTEST_OVERRIDE18=/sys/module/export_dep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH18=tests/tmp/sys/module/export_dep_$BITNESS/initstate
export MODTEST_OVERRIDE18 MODTEST_OVERRIDE_WITH18

MODTEST_OVERRIDE19=/sys/module/noexport_doubledep_$BITNESS
MODTEST_OVERRIDE_WITH19=tests/tmp/sys/module/noexport_doubledep_$BITNESS
export MODTEST_OVERRIDE19 MODTEST_OVERRIDE_WITH19

MODTEST_OVERRIDE20=/sys/module/noexport_doubledep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH20=tests/tmp/sys/module/noexport_doubledep_$BITNESS/initstate
export MODTEST_OVERRIDE20 MODTEST_OVERRIDE_WITH20

# Now create modules.dep
cat > tests/tmp/modules.dep <<EOF
# Should handle comments.
noexport_nodep-$BITNESS.ko:
noexport_doubledep-$BITNESS.ko: export_dep-$BITNESS.ko export_nodep-$BITNESS.ko
noexport_dep-$BITNESS.ko: export_nodep-$BITNESS.ko
export_nodep-$BITNESS.ko:
export_dep-$BITNESS.ko: export_nodep-$BITNESS.ko
EOF

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

# Removal
[ "`./modprobe -r noexport_nodep-$BITNESS export_nodep-$BITNESS 2>&1`" = "DELETE_MODULE: noexport_nodep_$BITNESS EXCL 
DELETE_MODULE: export_nodep_$BITNESS EXCL " ]

done
