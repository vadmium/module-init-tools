#! /bin/sh

# Test handling of /sys/module.

for BITNESS in 32 64; do

# Inputs
MODTEST_OVERRIDE1=/lib/modules/$MODTEST_UNAME
MODTEST_OVERRIDE_WITH1=tests/data/$BITNESS/normal
export MODTEST_OVERRIDE1 MODTEST_OVERRIDE_WITH1

MODTEST_OVERRIDE2=/lib/modules/$MODTEST_UNAME/noexport_nodep-$BITNESS.ko
MODTEST_OVERRIDE_WITH2=tests/data/$BITNESS/normal/noexport_nodep-$BITNESS.ko
export MODTEST_OVERRIDE2 MODTEST_OVERRIDE_WITH2

MODTEST_OVERRIDE3=/lib/modules/$MODTEST_UNAME/export_nodep-$BITNESS.ko
MODTEST_OVERRIDE_WITH3=tests/data/$BITNESS/normal/export_nodep-$BITNESS.ko
export MODTEST_OVERRIDE3 MODTEST_OVERRIDE_WITH3

MODTEST_OVERRIDE4=/lib/modules/$MODTEST_UNAME/modules.dep
MODTEST_OVERRIDE_WITH4=tests/tmp/modules.dep
export MODTEST_OVERRIDE4 MODTEST_OVERRIDE_WITH4

MODTEST_OVERRIDE5=/etc/modprobe.conf
MODTEST_OVERRIDE_WITH5=FILE-WHICH-DOESNT-EXIST
export MODTEST_OVERRIDE5 MODTEST_OVERRIDE_WITH5

MODTEST_OVERRIDE6=/lib/modules/$MODTEST_UNAME/modules.dep.bin
MODTEST_OVERRIDE_WITH6=FILE-WHICH-DOESNT-EXIST
export MODTEST_OVERRIDE6 MODTEST_OVERRIDE_WITH6

MODTEST_OVERRIDE7=/sys/module/noexport_nodep_$BITNESS
MODTEST_OVERRIDE_WITH7=tests/tmp/sys/module/noexport_nodep_$BITNESS
export MODTEST_OVERRIDE7 MODTEST_OVERRIDE_WITH7

MODTEST_OVERRIDE8=/sys/module/noexport_nodep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH8=tests/tmp/sys/module/noexport_nodep_$BITNESS/initstate
export MODTEST_OVERRIDE8 MODTEST_OVERRIDE_WITH8

MODTEST_OVERRIDE9=/sys/module/export_nodep_$BITNESS
MODTEST_OVERRIDE_WITH9=tests/tmp/sys/module/export_nodep_$BITNESS
export MODTEST_OVERRIDE9 MODTEST_OVERRIDE_WITH9

MODTEST_OVERRIDE10=/sys/module/export_nodep_$BITNESS/initstate
MODTEST_OVERRIDE_WITH10=tests/tmp/sys/module/export_nodep_$BITNESS/initstate
export MODTEST_OVERRIDE10 MODTEST_OVERRIDE_WITH10

MODTEST_OVERRIDE11=/sys/module
MODTEST_OVERRIDE_WITH11=tests/tmp/sys/module
export MODTEST_OVERRIDE11 MODTEST_OVERRIDE_WITH11

# Now create modules.dep
cat > tests/tmp/modules.dep <<EOF
noexport_nodep-$BITNESS.ko:
export_nodep-$BITNESS.ko:
EOF

#TODO: If we decided to do this later...
# Now create modules.dep.bin
#cat | ./modindex -o tests/tmp/modules.dep.bin <<EOF
#noexport_nodep-$BITNESS noexport_nodep-$BITNESS.ko:
#export_nodep-$BITNESS export_nodep-$BITNESS.ko:
#EOF

# TODO: make this more complete (like the original 02proc.sh)

SIZE_NOEXPORT_NODEP=$(echo `wc -c < tests/data/$BITNESS/normal/noexport_nodep-$BITNESS.ko`)
SIZE_EXPORT_NODEP=$(echo `wc -c < tests/data/$BITNESS/normal/export_nodep-$BITNESS.ko`)

# If it can't open /sys/module, it should try anyway.
rm -rf tests/tmp/sys/module

[ "`./modprobe noexport_nodep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_NOEXPORT_NODEP " ]
[ "`./modprobe export_nodep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP " ]

[ "`./modprobe -r noexport_nodep-$BITNESS 2>&1`" = "DELETE_MODULE: noexport_nodep_$BITNESS EXCL " ]
[ "`./modprobe -r export_nodep-$BITNESS 2>&1`" = "DELETE_MODULE: export_nodep_$BITNESS EXCL " ]

# Now make a fake /sys/module structure for the test
mkdir -p tests/tmp/sys/module
mkdir -p tests/tmp/sys/module/noexport_nodep_$BITNESS
mkdir -p tests/tmp/sys/module/export_nodep_$BITNESS
touch tests/tmp/sys/module/noexport_nodep_$BITNESS/initstate
touch tests/tmp/sys/module/export_nodep_$BITNESS/initstate

# Test load the modules

[ "`./modprobe noexport_nodep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_NOEXPORT_NODEP " ]
[ "`./modprobe export_nodep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP " ]

# Now make a fake /sys/module structure for the test
mkdir -p tests/tmp/sys/module
mkdir -p tests/tmp/sys/module/noexport_nodep_$BITNESS
mkdir -p tests/tmp/sys/module/export_nodep_$BITNESS
touch tests/tmp/sys/module/noexport_nodep_$BITNESS/initstate
touch tests/tmp/sys/module/export_nodep_$BITNESS/initstate

# Test remove the modules

[ "`./modprobe -r noexport_nodep-$BITNESS 2>&1`" = "DELETE_MODULE: noexport_nodep_$BITNESS EXCL " ]
[ "`./modprobe -r export_nodep-$BITNESS 2>&1`" = "DELETE_MODULE: export_nodep_$BITNESS EXCL " ]

done
