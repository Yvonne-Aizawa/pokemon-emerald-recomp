cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS_DEBUG="-g -O0 -fno-omit-frame-pointer"
cmake --build build-dbg -j
gdb -batch -ex run -ex bt -ex "bt full 6" -ex "info registers" -ex "x/6i \$pc" ./build-dbg/pkmemerald 2>&1 | tee crash.txt
