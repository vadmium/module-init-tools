#! /bin/sh

for BITNESS in 32 64; do

rm -rf tests/tmp/*

# Create a simple config file.
cat > tests/tmp/modprobe.conf <<EOF
EOF

# Create inputs
MODULE_DIR=tests/tmp/lib/modules/$MODTEST_UNAME
mkdir -p $MODULE_DIR
ln tests/data/$BITNESS$ENDIAN/normal/export_nodep-$BITNESS.ko \
   tests/data/$BITNESS$ENDIAN/normal/noexport_nodep-$BITNESS.ko \
   $MODULE_DIR

# Now create modules.dep
cat > $MODULE_DIR/modules.dep <<EOF
export_nodep-$BITNESS.ko:
noexport_nodep-$BITNESS.ko:
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

# Slow disks (e.g. first generation SSDs) can cause long delays
# Try to avoid modprobe being delayed during this test
sync

MODPROBE_WAIT=tests/tmp/continue
export MODPROBE_WAIT

SIZE=$(echo `wc -c < tests/data/$BITNESS/normal/export_nodep-$BITNESS.ko`)

# Should be looping.
./modprobe export_nodep-$BITNESS > tests/tmp/out1 2>&1 &
sleep 1

[ "`cat tests/tmp/out1`" = "Looping on tests/tmp/continue" ]

# Second one should wait.
./modprobe -r export_nodep-$BITNESS > tests/tmp/out2 2>&1 &
sleep 1

[ "`cat tests/tmp/out2`" = "" ]

# Release first one
touch tests/tmp/continue
sleep 1

# Should have exited and cleaned up
[ "`cat tests/tmp/out1`" = "Looping on tests/tmp/continue
Removing tests/tmp/continue
INIT_MODULE: $SIZE " ]
[ ! -f tests/tmp/continue ]

# Second one should now be looping.
[ "`cat tests/tmp/out2`" = "Looping on tests/tmp/continue" ]

# Release second one
touch tests/tmp/continue
sleep 1

# Should have exited and cleaned up
[ "`cat tests/tmp/out2`" = "Looping on tests/tmp/continue
Removing tests/tmp/continue
DELETE_MODULE: export_nodep_$BITNESS EXCL " ]
[ ! -f tests/tmp/continue ]

# Lock gets dropped for install commands
mkdir -p tests/tmp/etc
cat > tests/tmp/etc/modprobe.conf <<EOF
# Aliases to cause one command to hang.
install export_nodep-$BITNESS ./modprobe --ignore-install export_nodep-$BITNESS foo
remove export_nodep-$BITNESS ./modprobe -r --ignore-remove export_nodep-$BITNESS
EOF

MODTEST_DO_SYSTEM=1
export MODTEST_DO_SYSTEM

touch tests/tmp/continue
[ "`./modprobe export_nodep-$BITNESS`" = "Looping on tests/tmp/continue
Removing tests/tmp/continue
INIT_MODULE: $SIZE foo" ]

touch tests/tmp/continue
[ "`./modprobe -r export_nodep-$BITNESS`" = "Looping on tests/tmp/continue
Removing tests/tmp/continue
DELETE_MODULE: export_nodep_$BITNESS EXCL " ]

# Check that a read-only module still loads.
cp tests/data/$BITNESS/normal/noexport_nodep-$BITNESS.ko tests/tmp/
chmod a-w tests/tmp/noexport_nodep-$BITNESS.ko
unset MODPROBE_WAIT

SIZE2=$(echo `wc -c < tests/tmp/noexport_nodep-$BITNESS.ko`)

[ "`./modprobe noexport_nodep-$BITNESS`" = "INIT_MODULE: $SIZE2 " ]
done
