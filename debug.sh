#rm build -rf
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_FLAGS_DEBUG="-g -O0 -fno-omit-frame-pointer"
cmake --build build -j
gdb -batch -ex run -ex bt -ex "bt full 6" -ex "info registers" -ex "x/6i \$pc" ./build/pkmemerald 2>&1 | tee crash.txt
