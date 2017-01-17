#!/bin/sh

# Script for adding entries for header files to clang compilation db
# (compile_commands.json). Useful for, among others, irony-mode

set -e

compdb=`which compdb 2> /dev/null || true`
if [ "x${compdb}" == "x" ]; then
    echo "Couldn't find compdb. Install with pop install compdb"
    exit 1
fi

if [ $# -lt 1 ]; then
    echo "Usage compdb_amend_headers.sh <dir>"
    exit 1
fi

compdb_file="compile_commands.json"
compdb_path="${1}/build/${compdb_file}"
if [ ! -f $compdb_path ]; then
    echo "No such file ${compdb_path}"
    exit 1
fi

build_dir="${1}/build"

dir=`mktemp -d`
$compdb headerdb -p $build_dir > ${dir}/${compdb_file}
dir2=`mktemp -d`
cp ${compdb_path} ${dir2}/
$compdb dump -p ${dir} -p ${dir2} > ${compdb_path}

rm ${dir}/${compdb_file}
rm ${dir2}/${compdb_file}
rmdir ${dir}
rmdir ${dir2}

exit 0
