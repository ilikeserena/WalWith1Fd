#cmake -DCMAKE_RULE_MESSAGES:BOOL=OFF -DCMAKE_VERBOSE_MAKEFILE:BOOL=ON ../src
#make --no-print-directory

set -ve
mkdir -p Build
cd Build
cmake ../src
make
./SQLiteTest

