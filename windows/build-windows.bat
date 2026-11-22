@echo off
setlocal
rem Requires MinGW-w64 (x86_64-w64-mingw32-gcc) in PATH.
set CC=x86_64-w64-mingw32-gcc
set CFLAGS=-O3 -std=c11 -Wall -Wextra -static
if not exist build mkdir build

echo Building CPU Compute...
%CC% %CFLAGS% -march=haswell -DBENCH_FLAGS="\"-O3 -march=haswell (Windows AVX2)\"" ^
  bench_win.c -o build\cpu-bench-x86_64-avx2.exe -ladvapi32 -luser32 -lm
%CC% %CFLAGS% -march=x86-64 -mtune=generic -DBENCH_FLAGS="\"-O3 -march=x86-64 (Windows generic)\"" ^
  bench_win.c -o build\cpu-bench-x86_64-generic.exe -ladvapi32 -luser32 -lm

echo Building Application...
%CC% %CFLAGS% -march=x86-64 -mtune=generic ^
  ..\application\application_bench.c -o build\application-bench.exe -luser32 -lm

echo Building Memory...
%CC% %CFLAGS% -march=x86-64 -mtune=generic ^
  ..\memory\memory_bench.c -o build\memory-bench.exe -luser32 -lm

echo Done. Output in build\
endlocal
