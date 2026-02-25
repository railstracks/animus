cmake -S . -B build && cmake --build build
ctest --test-dir build
./dist/bin/animusd --test-jobs
./dist/bin/animusd --load-hello-module
