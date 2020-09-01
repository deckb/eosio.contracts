
if [ $# -ne 3 ]; then
    echo "Usage: ./build_and_run.sh eosio_system_rentbw_tests/model_tests model_config_foo.json csv_output_foo.csv"
    exit 1
fi

mkdir -p build
pushd build
set -e
cmake -DBUILD_TESTS=true -DCMAKE_PREFIX_PATH=~/eosio/2.1/ -DBOOST_ROOT=~/eosio/2.1/src/boost_1_72_0 ..
make -j
set +e
popd

cp $2 _model_config.json
build/tests/unit_test -t $1
mv model_tests.csv $3

