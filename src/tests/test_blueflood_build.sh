SCRIPT=$(readlink -f "$0")
CURDIR=`dirname "$SCRIPT"`

GCOV_FLAGS="-Wdisabled-optimization -O0 --coverage -fprofile-arcs -ftest-coverage"
GCOV_LDFLAGS="-fprofile-arcs"

#real curl
CFLAGS="-I./src -I./src/daemon -D HAVE_CONFIG_H -g -O0 -Werror -Wall $GCOV_FLAGS"
echo "run tests without auth"
rm test_blueflood2 ./src/tests/test_blueflood1.o ./src/tests/test_blueflood_mock.o -f
find -name "*.gcno" -or -name "*.gcda" | xargs rm -f
OBJECTS=
for file in "src/daemon/common" "src/tests/mock/utils_time" "src/tests/mock/utils_cache"
do
    gcc -c $file.c -o $file.o $CFLAGS
    OBJECTS="$OBJECTS $file.o"
done
gcc -c ./src/tests/test_blueflood_mock.c -o ./src/tests/test_blueflood_mock.o $CFLAGS
gcc -c ./src/tests/mock/plugin.c -o ./src/tests/mock/plugin.o $CFLAGS
gcc -c ./src/tests/test_blueflood1.c -o ./src/tests/test_blueflood1.o $CFLAGS
gcc -o $CURDIR/test_blueflood1 ./src/tests/test_blueflood1.o ./src/tests/mock/plugin.o $OBJECTS ./src/tests/test_blueflood_mock.o -lpthread $GCOV_LDFLAGS
#check compilation error
if [ $? -ne 0 ]; then
    exit
fi
#run test
$CURDIR/test_blueflood1

echo "run tests with auth"
rm test_blueflood2 ./src/tests/test_blueflood2.o ./src/tests/test_blueflood_mock.o -f
gcc -c ./src/tests/test_blueflood2.c -o ./src/tests/test_blueflood2.o $CFLAGS
gcc -c ./src/tests/test_blueflood_mock.c -o ./src/tests/test_blueflood_mock.o $CFLAGS
gcc -o $CURDIR/test_blueflood2 ./src/tests/test_blueflood2.o ./src/tests/mock/plugin.o $OBJECTS ./src/tests/test_blueflood_mock.o -lpthread $GCOV_LDFLAGS
#check compilation error
if [ $? -ne 0 ]; then
    exit
fi
#run test
$CURDIR/test_blueflood2

GCOV_HTML_FOLDER=$CURDIR/blueflood_coverage_html
#prepare html document covering only sources from lib folder
lcov --gcov-tool=gcov --directory=$CURDIR --capture --output-file $GCOV_HTML_FOLDER/app.info
genhtml --output-directory $GCOV_HTML_FOLDER $GCOV_HTML_FOLDER/app.info

echo open $GCOV_HTML_FOLDER/index.html


#just functional tests should not affect tests coverage
rm test_blueflood3 ./src/tests/test_blueflood3.o -f
gcc -c ./src/tests/mock/plugin.c -o ./src/tests/mock/plugin.o $CFLAGS
gcc -c ./src/tests/test_blueflood3.c -o ./src/tests/test_blueflood3.o $CFLAGS
gcc -o $CURDIR/test_blueflood3 ./src/tests/test_blueflood3.o ./src/tests/mock/plugin.o $OBJECTS -lyajl -lcurl -lpthread $GCOV_LDFLAGS
#check compilation error
if [ $? -ne 0 ]; then
    exit
fi
#run test
$CURDIR/test_blueflood3
