#! /bin/sh
# Test module dependencies.

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

MODTEST_OVERRIDE9=/lib/modules/$MODTEST_UNAME/modules.dep
MODTEST_OVERRIDE_WITH9=tests/tmp/modules.dep
export MODTEST_OVERRIDE9 MODTEST_OVERRIDE_WITH9

MODTEST_OVERRIDE10=/lib/modules/$MODTEST_UNAME/modules.dep.bin
MODTEST_OVERRIDE_WITH10=tests/tmp/modules.dep.bin
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

MODTEST_OVERRIDE21=/sys/module/newname
MODTEST_OVERRIDE_WITH21=tests/tmp/sys/module/newname
export MODTEST_OVERRIDE21 MODTEST_OVERRIDE_WITH21

MODTEST_OVERRIDE22=/sys/module/newname/initstate
MODTEST_OVERRIDE_WITH22=tests/tmp/sys/module/newname/initstate
export MODTEST_OVERRIDE22 MODTEST_OVERRIDE_WITH22

MODTEST_OVERRIDE23=/sys/module/newname/refcnt
MODTEST_OVERRIDE_WITH23=tests/tmp/sys/module/newname/refcnt
export MODTEST_OVERRIDE23 MODTEST_OVERRIDE_WITH23

# Now create modules.dep.bin
cat > tests/tmp/modules.dep.bin.temp <<EOF
noexport_nodep_$BITNESS noexport_nodep-$BITNESS.ko:
noexport_doubledep_$BITNESS noexport_doubledep-$BITNESS.ko: export_dep-$BITNESS.ko export_nodep-$BITNESS.ko
noexport_dep_$BITNESS noexport_dep-$BITNESS.ko: export_nodep-$BITNESS.ko
export_nodep_$BITNESS export_nodep-$BITNESS.ko:
export_dep_$BITNESS export_dep-$BITNESS.ko: export_nodep-$BITNESS.ko
EOF
./modindex -o tests/tmp/modules.dep.bin < tests/tmp/modules.dep.bin.temp

# Insertion
SIZE_NOEXPORT_NODEP=$(echo `wc -c < tests/data/$BITNESS/normal/noexport_nodep-$BITNESS.ko`)
SIZE_EXPORT_NODEP=$(echo `wc -c < tests/data/$BITNESS/normal/export_nodep-$BITNESS.ko`)
SIZE_NOEXPORT_DEP=$(echo `wc -c < tests/data/$BITNESS/normal/noexport_dep-$BITNESS.ko`)
SIZE_EXPORT_DEP=$(echo `wc -c < tests/data/$BITNESS/normal/export_dep-$BITNESS.ko`)
SIZE_NOEXPORT_DOUBLEDEP=$(echo `wc -c < tests/data/$BITNESS/normal/noexport_doubledep-$BITNESS.ko`)

# Empty sysfs
rm -rf tests/tmp/sys

[ "`./modprobe noexport_nodep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_NOEXPORT_NODEP " ]
[ "`./modprobe noexport_nodep-$BITNESS OPTIONS 2>&1`" = "INIT_MODULE: $SIZE_NOEXPORT_NODEP OPTIONS" ]

[ "`./modprobe export_nodep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP " ]
[ "`./modprobe export_nodep-$BITNESS OPTIONS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP OPTIONS" ]

[ "`./modprobe noexport_dep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP 
INIT_MODULE: $SIZE_NOEXPORT_DEP " ]
[ "`./modprobe noexport_dep-$BITNESS OPTIONS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP 
INIT_MODULE: $SIZE_NOEXPORT_DEP OPTIONS" ]

[ "`./modprobe export_dep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP 
INIT_MODULE: $SIZE_EXPORT_DEP " ]
[ "`./modprobe export_dep-$BITNESS OPTIONS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP 
INIT_MODULE: $SIZE_EXPORT_DEP OPTIONS" ]

[ "`./modprobe noexport_doubledep-$BITNESS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP 
INIT_MODULE: $SIZE_EXPORT_DEP 
INIT_MODULE: $SIZE_NOEXPORT_DOUBLEDEP " ]
[ "`./modprobe noexport_doubledep-$BITNESS OPTIONS 2>&1`" = "INIT_MODULE: $SIZE_EXPORT_NODEP 
INIT_MODULE: $SIZE_EXPORT_DEP 
INIT_MODULE: $SIZE_NOEXPORT_DOUBLEDEP OPTIONS" ]

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
[ "`./modprobe -r noexport_nodep-$BITNESS 2>&1`" = "DELETE_MODULE: noexport_nodep_$BITNESS EXCL " ]
[ "`./modprobe -r export_nodep-$BITNESS 2>&1`" = "DELETE_MODULE: export_nodep_$BITNESS EXCL " ]
[ "`./modprobe -r noexport_dep-$BITNESS 2>&1`" = "DELETE_MODULE: noexport_dep_$BITNESS EXCL 
DELETE_MODULE: export_nodep_$BITNESS EXCL " ]
[ "`./modprobe -r export_dep-$BITNESS 2>&1`" = "DELETE_MODULE: export_dep_$BITNESS EXCL 
DELETE_MODULE: export_nodep_$BITNESS EXCL " ]
[ "`./modprobe -r noexport_doubledep-$BITNESS 2>&1`" = "DELETE_MODULE: noexport_doubledep_$BITNESS EXCL 
DELETE_MODULE: export_dep_$BITNESS EXCL 
DELETE_MODULE: export_nodep_$BITNESS EXCL " ]

# Add in newname to /sys/module
mkdir -p tests/tmp/sys/module/newname
echo "live" > tests/tmp/sys/module/newname/initstate
touch tests/tmp/sys/module/newname/refcnt

[ "`./modprobe -o newname -r noexport_doubledep-$BITNESS 2>&1`" = "DELETE_MODULE: newname EXCL 
DELETE_MODULE: export_dep_$BITNESS EXCL 
DELETE_MODULE: export_nodep_$BITNESS EXCL " ]

done
