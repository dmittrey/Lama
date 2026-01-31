  $ echo "=== Testing: source-level interpreter (-i), AST interpretation ==="
  === Testing: source-level interpreter (-i), AST interpretation ===
  $ start=$(perl -MTime::HiRes=time -e 'print time()'); ../src/Driver.exe -runtime ../runtime -I ../stdlib/x64 -i Sort.lama < Sort.input; end=$(perl -MTime::HiRes=time -e 'print time()'); perl -e 'printf "finished in %.3f ms\n", ($ARGV[1]-$ARGV[0])*1000' "$start" "$end"
  finished in 0.000 ms
