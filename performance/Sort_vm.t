  $ echo "=== Testing: custom VM — bytecode from file (.bc) ==="
  === Testing: custom VM — bytecode from file (.bc) ===
  $ ../src/Driver.exe -runtime ../runtime -I ../stdlib/x64 -b Sort.lama
  $ start=$(perl -MTime::HiRes=time -e 'print time()'); ../vm/vm Sort.bc < Sort.input; end=$(perl -MTime::HiRes=time -e 'print time()'); perl -e 'printf "finished in %.3f ms\n", ($ARGV[1]-$ARGV[0])*1000' "$start" "$end"
  finished in 0.000 ms
