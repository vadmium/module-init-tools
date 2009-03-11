#! /bin/sh
# Test the module renaming code.

for BITNESS in 32 64; do

rm -rf tests/tmp/*

# We need to dump the module to make sure the name has changed.
MODTEST_DUMP_INIT=1
export MODTEST_DUMP_INIT

# Create inputs
MODULE_DIR=tests/tmp/lib/modules/$MODTEST_UNAME
mkdir -p $MODULE_DIR
ln tests/data/$BITNESS/rename/rename-new-$BITNESS.ko \
   tests/data/$BITNESS/rename/rename-old-$BITNESS.ko \
   $MODULE_DIR

# Set up modules.dep file (neither has dependencies).
echo "# A comment" > $MODULE_DIR/modules.dep
echo "/lib/modules/$MODTEST_UNAME/rename-new-$BITNESS.ko:" >> $MODULE_DIR/modules.dep
echo "/lib/modules/$MODTEST_UNAME/rename-old-$BITNESS.ko:" >> $MODULE_DIR/modules.dep

# Test old-style module 
[ "`modprobe rename-old-$BITNESS 2> tests/tmp/out`" = "" ]
strings tests/tmp/out | grep -q 'rename_old'
if strings tests/tmp/out | grep -q 'short'; then exit 1; fi

[ "`modprobe -o short rename-old-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_old'; then exit 1; fi
strings tests/tmp/out | grep -q 'short'

[ "`modprobe -o very_very_long_name rename-old-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_old'; then exit 1; fi
strings tests/tmp/out | grep -q 'very_very_long_name'

[ "`modprobe -o short rename-old-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_old'; then exit 1; fi
strings tests/tmp/out | grep -q 'short'

[ "`modprobe -o very_very_long_name rename-old-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_old'; then exit 1; fi
strings tests/tmp/out | grep -q 'very_very_long_name'

[ "`modprobe --name short rename-old-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_old'; then exit 1; fi
strings tests/tmp/out | grep -q 'short'

[ "`modprobe --name very_very_long_name rename-old-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_old'; then exit 1; fi
strings tests/tmp/out | grep -q 'very_very_long_name'

[ "`modprobe --name=short rename-old-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_old'; then exit 1; fi
strings tests/tmp/out | grep -q 'short'

[ "`modprobe --name=very_very_long_name rename-old-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_old'; then exit 1; fi
strings tests/tmp/out | grep -q 'very_very_long_name'

# Test new-style module 
[ "`modprobe rename-new-$BITNESS 2> tests/tmp/out`" = "" ]
strings tests/tmp/out | grep -q 'rename_new'
if strings tests/tmp/out | grep -q 'short'; then exit 1; fi

[ "`modprobe -o short rename-new-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_new'; then exit 1; fi
strings tests/tmp/out | grep -q 'short'

[ "`modprobe -o very_very_long_name rename-new-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_new'; then exit 1; fi
strings tests/tmp/out | grep -q 'very_very_long_name'

[ "`modprobe -o short rename-new-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_new'; then exit 1; fi
strings tests/tmp/out | grep -q 'short'

[ "`modprobe -o very_very_long_name rename-new-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_new'; then exit 1; fi
strings tests/tmp/out | grep -q 'very_very_long_name'

[ "`modprobe --name short rename-new-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_new'; then exit 1; fi
strings tests/tmp/out | grep -q 'short'

[ "`modprobe --name very_very_long_name rename-new-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_new'; then exit 1; fi
strings tests/tmp/out | grep -q 'very_very_long_name'

[ "`modprobe --name=short rename-new-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_new'; then exit 1; fi
strings tests/tmp/out | grep -q 'short'

[ "`modprobe --name=very_very_long_name rename-new-$BITNESS 2> tests/tmp/out`" = "" ]
if strings tests/tmp/out | grep -q 'rename_new'; then exit 1; fi
strings tests/tmp/out | grep -q 'very_very_long_name'

done
