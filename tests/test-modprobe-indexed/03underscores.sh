#! /bin/sh
# Check underscore synonymity everywhere.

for BITNESS in 32 64; do

MODTEST_OVERRIDE1=/lib/modules/$MODTEST_UNAME/modules.dep.bin
MODTEST_OVERRIDE_WITH1=tests/tmp/modules.dep.bin
export MODTEST_OVERRIDE1 MODTEST_OVERRIDE_WITH1

MODTEST_OVERRIDE2=/lib/modules/$MODTEST_UNAME/noexport_nodep-$BITNESS.ko
MODTEST_OVERRIDE_WITH2=tests/data/$BITNESS/normal/noexport_nodep-$BITNESS.ko
export MODTEST_OVERRIDE2 MODTEST_OVERRIDE_WITH2

MODTEST_OVERRIDE3=/etc/modprobe.conf
MODTEST_OVERRIDE_WITH3=tests/tmp/modprobe.conf
export MODTEST_OVERRIDE3 MODTEST_OVERRIDE_WITH3

MODTEST_OVERRIDE4=include-_
MODTEST_OVERRIDE_WITH4=tests/tmp/modprobe.conf.included
export MODTEST_OVERRIDE4 MODTEST_OVERRIDE_WITH4

MODTEST_OVERRIDE5=/proc/modules
MODTEST_OVERRIDE_WITH5=FILE-WHICH-DOESNT-EXIST
export MODTEST_OVERRIDE5 MODTEST_OVERRIDE_WITH5

MODTEST_OVERRIDE6=/lib/modules/$MODTEST_UNAME/export_nodep-$BITNESS.ko
MODTEST_OVERRIDE_WITH6=tests/data/$BITNESS/normal/export_nodep-$BITNESS.ko
export MODTEST_OVERRIDE6 MODTEST_OVERRIDE_WITH6

MODTEST_OVERRIDE7=/sys/module/noexport_nodep_$BITNESS
MODTEST_OVERRIDE_WITH7=tests/tmp/sys/module/noexport_nodep_$BITNESS
export MODTEST_OVERRIDE7 MODTEST_OVERRIDE_WITH7

MODTEST_OVERRIDE8=/sys/module/noexport_nodep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH8=tests/tmp/sys/module/noexport_nodep_$BITNESS/initstate
export MODTEST_OVERRIDE8 MODTEST_OVERRIDE_WITH8

MODTEST_OVERRIDE9=/sys/module/noexport_dep_$BITNESS
MODTEST_OVERRIDE_WITH9=tests/tmp/sys/module/noexport_dep_$BITNESS
export MODTEST_OVERRIDE9 MODTEST_OVERRIDE_WITH9

MODTEST_OVERRIDE10=/sys/module/noexport_dep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH10=tests/tmp/sys/module/noexport_dep_$BITNESS/initstate
export MODTEST_OVERRIDE10 MODTEST_OVERRIDE_WITH10

MODTEST_OVERRIDE11=/sys/module/export_nodep_$BITNESS
MODTEST_OVERRIDE_WITH11=tests/tmp/sys/module/export_nodep_$BITNESS
export MODTEST_OVERRIDE11 MODTEST_OVERRIDE_WITH11

MODTEST_OVERRIDE12=/sys/module/export_nodep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH12=tests/tmp/sys/module/export_nodep_$BITNESS/initstate
export MODTEST_OVERRIDE12 MODTEST_OVERRIDE_WITH12

MODTEST_OVERRIDE13=/sys/module/export_dep_$BITNESS
MODTEST_OVERRIDE_WITH13=tests/tmp/sys/module/export_dep_$BITNESS
export MODTEST_OVERRIDE13 MODTEST_OVERRIDE_WITH13

MODTEST_OVERRIDE14=/sys/module/export_dep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH14=tests/tmp/sys/module/export_dep_$BITNESS/initstate
export MODTEST_OVERRIDE14 MODTEST_OVERRIDE_WITH14

MODTEST_OVERRIDE15=/sys/module/noexport_doubledep_$BITNESS
MODTEST_OVERRIDE_WITH15=tests/tmp/sys/module/noexport_doubledep_$BITNESS
export MODTEST_OVERRIDE15 MODTEST_OVERRIDE_WITH15

MODTEST_OVERRIDE16=/sys/module/noexport_doubledep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH16=tests/tmp/sys/module/noexport_doubledep_$BITNESS/initstate
export MODTEST_OVERRIDE16 MODTEST_OVERRIDE_WITH16

# Set up modules.dep.bin file.
cat > tests/tmp/modules.dep.bin.temp <<EOF
noexport_nodep_$BITNESS noexport_nodep-$BITNESS.ko:
export_nodep_$BITNESS export_nodep-$BITNESS.ko:
EOF
./modindex -o tests/tmp/modules.dep.bin < tests/tmp/modules.dep.bin.temp

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

# Set up config file.
echo "alias alias-_ noexport-nodep_$BITNESS" > tests/tmp/modprobe.conf
echo "options export-nodep_$BITNESS option-_" >> tests/tmp/modprobe.conf
echo "install test-_ echo install-_" >> tests/tmp/modprobe.conf
echo "remove test-_ echo remove-_" >> tests/tmp/modprobe.conf
echo "include include-_" >> tests/tmp/modprobe.conf
echo "install test-include echo Included" >> tests/tmp/modprobe.conf.included

SIZE1=$(echo `wc -c < tests/data/$BITNESS/normal/noexport_nodep-$BITNESS.ko`)
SIZE2=$(echo `wc -c < tests/data/$BITNESS/normal/export_nodep-$BITNESS.ko`)

# On command line (-r and normal)
[ "`./modprobe noexport-nodep_$BITNESS 2>&1`" = "INIT_MODULE: $SIZE1 " ]
[ "`./modprobe -r noexport-nodep_$BITNESS 2>&1`" = "DELETE_MODULE: noexport_nodep_$BITNESS EXCL " ]

# In alias commands (source and target)
[ "`./modprobe alias-_ 2>&1`" = "INIT_MODULE: $SIZE1 " ]
[ "`./modprobe alias_- 2>&1`" = "INIT_MODULE: $SIZE1 " ]
[ "`./modprobe -r alias-_ 2>&1`" = "DELETE_MODULE: noexport_nodep_$BITNESS EXCL " ]
[ "`./modprobe -r alias_- 2>&1`" = "DELETE_MODULE: noexport_nodep_$BITNESS EXCL " ]

# In option commands (NOT in arguments)
[ "`./modprobe export_nodep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE2 option-_" ]
[ "`./modprobe export-nodep_$BITNESS 2>&1`" = "INIT_MODULE: $SIZE2 option-_" ]

# In install commands
[ "`./modprobe test-_ 2>&1`" = "SYSTEM: echo install-_" ]
[ "`./modprobe test_- 2>&1`" = "SYSTEM: echo install-_" ]

# In remove commands
[ "`./modprobe -r test-_ 2>&1`" = "SYSTEM: echo remove-_" ]
[ "`./modprobe -r test_- 2>&1`" = "SYSTEM: echo remove-_" ]

# NOT in include commands
[ "`./modprobe test-include 2>&1`" = "SYSTEM: echo Included" ]
[ "`./modprobe test_include 2>&1`" = "SYSTEM: echo Included" ]

done
