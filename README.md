# v8interpreter
v8 wrapper with "smart" template based bindings

## OSX Quick test

    brew install v8
    g++ -DTESTME -std=c++11 -I/usr/local/Cellar/v8/4.5.103.35 -L/usr/local/opt/icu4c/lib -lv8_libplatform -lv8_base -lv8_libbase -lv8_snapshot -licudata -licuuc -licui18n v8interpreter.cpp -ov8test
