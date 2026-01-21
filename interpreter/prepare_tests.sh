#!/bin/bash

perl -pi -e 's/^(\s*)\$\s+\.\.\/src\/Driver\.exe\s+-runtime\s+\.\.\/runtime\s+-I\s+\.\.\/stdlib\/x64\s+-i\s+(test\d+)\.lama\s+<\s+\2\.input\s*$/$1\$ ..\/src\/Driver.exe -runtime ..\/runtime -I ..\/stdlib\/x64 -b $2.lama && ..\/interpreter\/interpreter $2.bc < $2.input\n/;' regression/test*.t
