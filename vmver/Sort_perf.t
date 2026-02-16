  $ echo "=== Testing: vmver — bytecode from file (.bc) with timing ==="
  === Testing: vmver — bytecode from file (.bc) with timing ===
  $ ../src/Driver.exe -runtime ../runtime -I ../stdlib/x64 -b performance/Sort.lama
  $ start=$(perl -MTime::HiRes=time -e 'print time()'); ./vmver Sort.bc < performance/Sort.input; end=$(perl -MTime::HiRes=time -e 'print time()'); perl -e 'printf "finished in %.3f ms\n", ($ARGV[1]-$ARGV[0])*1000' "$start" "$end"
  finished in 778908.154 ms

  # bytecode interpretation (-s) - finished in 295270.730 ms
  # source-level interpreter (-i), AST interpretation - finished in 753908.150 ms
  # custom VM — bytecode from file (.bc) - finished in 1727814.300 ms
