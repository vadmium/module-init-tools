#! /bin/sh

for BITNESS in 32 64; do

# Inputs
MODTEST_OVERRIDE1=/lib/modules/$MODTEST_UNAME
MODTEST_OVERRIDE_WITH1=tests/data/$BITNESS/normal
export MODTEST_OVERRIDE1 MODTEST_OVERRIDE_WITH1

MODTEST_OVERRIDE2=/lib/modules/$MODTEST_UNAME/export_nodep-$BITNESS.ko
MODTEST_OVERRIDE_WITH2=tests/data/$BITNESS/normal/export_nodep-$BITNESS.ko
export MODTEST_OVERRIDE2 MODTEST_OVERRIDE_WITH2

MODTEST_OVERRIDE3=/lib/modules/$MODTEST_UNAME/noexport_dep-$BITNESS.ko
MODTEST_OVERRIDE_WITH3=tests/data/$BITNESS/normal/noexport_dep-$BITNESS.ko
export MODTEST_OVERRIDE3 MODTEST_OVERRIDE_WITH3

MODTEST_OVERRIDE4=/lib/modules/$MODTEST_UNAME/modules.dep
MODTEST_OVERRIDE_WITH4=tests/tmp/modules.dep
export MODTEST_OVERRIDE4 MODTEST_OVERRIDE_WITH4

MODTEST_OVERRIDE5=/etc/modprobe.conf
MODTEST_OVERRIDE_WITH5=tests/tmp/modprobe.conf
export MODTEST_OVERRIDE5 MODTEST_OVERRIDE_WITH5

MODTEST_OVERRIDE6=/lib/modules/$MODTEST_UNAME/modules.dep.bin
MODTEST_OVERRIDE_WITH6=FILE-WHICH-DOES-NOT-EXIST
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

# Now create modules.dep
cat > tests/tmp/modules.dep <<EOF
noexport_dep-$BITNESS.ko: export_nodep-$BITNESS.ko
export_nodep-$BITNESS.ko:
EOF

# Insertion
SIZE_EXPORT_NODEP=$(echo `wc -c < tests/data/$BITNESS/normal/export_nodep-$BITNESS.ko`)
SIZE_NOEXPORT_DEP=$(echo `wc -c < tests/data/$BITNESS/normal/noexport_dep-$BITNESS.ko`)

# Empty sysfs
rm -rf tests/tmp/sys

# Check it pulls in both.
[ "`./modprobe noexport_dep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP 
INIT_MODULE: $SIZE_NOEXPORT_DEP " ]

# Check it's happy if we tell it dep is already instealled
mkdir -p tests/tmp/sys/module
mkdir -p tests/tmp/sys/module/export_nodep_$BITNESS
echo "live" >tests/tmp/sys/module/export_nodep_$BITNESS/initstate

[ "`./modprobe noexport_dep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_NOEXPORT_DEP " ]

# If there's an install command, it will be done.
# Clean up sysfs (so we don't think it's loaded)
rm -rf tests/tmp/sys

echo "install export_nodep-$BITNESS COMMAND" > tests/tmp/modprobe.conf
[ "`./modprobe noexport_dep-$BITNESS 2>&1`" = "SYSTEM: COMMAND
INIT_MODULE: $SIZE_NOEXPORT_DEP " ]

# If it's in /sys/module, install command WONT be done.
mkdir -p tests/tmp/sys/module
mkdir -p tests/tmp/sys/module/export_nodep_$BITNESS
echo "live" >tests/tmp/sys/module/export_nodep_$BITNESS/initstate

[ "`./modprobe noexport_dep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_NOEXPORT_DEP " ]

# Do dependencies even if install command.
# clean up sysfs (so we don't think it's loaded)
rm -rf tests/tmp/sys

echo "install noexport_dep-$BITNESS COMMAND" > tests/tmp/modprobe.conf

[ "`./modprobe noexport_dep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP 
SYSTEM: COMMAND" ]

done
