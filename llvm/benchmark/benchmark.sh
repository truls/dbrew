#!/bin/bash

if [ $# -ne 3 ]
then
    echo "Usage: `basename $0` resultsdir executable iterations"
    exit 1
fi

resultsdir=$1
gatherfile=$resultsdir/gather-run.out
executable=$2
RUNS=$3
FPREC=4

mkdir -p $resultsdir
cp $executable $resultsdir/executable
: > $gatherfile

for mode in 0 1 2 3 4 5
do
    for config in 0 1 2 3 4 5
    do
        echo "Running $mode-$config ($RUNS)"
        file=$resultsdir/run-$mode-$config.out
        compilefile=$resultsdir/compile-$mode-$config.out
        $resultsdir/executable $config $mode 1 $RUNS 2>/dev/null > $file
        $resultsdir/executable $config $mode 1 $RUNS 2>/dev/null >> $file
        $resultsdir/executable $config $mode 1 $RUNS 2>/dev/null >> $file
        $resultsdir/executable $config $mode 1 0 1 2> /dev/null > $compilefile

        times=`grep Mode "${file}" | cut -d " " -f8`
        sum=0.; count=0; meant=-1
        for t in $times; do
            sum=`echo "scale=${FPREC}; ${sum} + ${t}" | bc`
            count=`echo "${count} + 1" | bc`
        done
        if [[ $sum > 0. ]] ; then
            meant=`echo "scale=${FPREC}; ${sum} / ${count}" | bc`
        fi
        echo $mode $config $RUNS $meant >> $gatherfile
    done
done
