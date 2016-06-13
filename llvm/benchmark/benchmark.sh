
RUNS=1
COMPILES=1

mkdir -p results

for interlines in 0
do
    make INTERLINES=$interlines
    for mode in 0 1 2 3 4 5
    do
        for config in 0 1 2 3 4 5 6 7 8
        do
            echo "Running $interlines-$mode-$config"
            ./benchmark-$interlines.o $config $mode 1 $RUNS 2>/dev/null > results/run-$interlines-$mode-$config.out
            ./benchmark-$interlines.o $config $mode 1 $RUNS 2>/dev/null >> results/run-$interlines-$mode-$config.out
            ./benchmark-$interlines.o $config $mode 1 $RUNS 2>/dev/null >> results/run-$interlines-$mode-$config.out
            ./benchmark-$interlines.o $config $mode $COMPILES 0 2>/dev/null > results/compiles-$interlines-$mode-$config.out
            ./benchmark-$interlines.o $config $mode $COMPILES 0 2>/dev/null >> results/compiles-$interlines-$mode-$config.out
            ./benchmark-$interlines.o $config $mode $COMPILES 0 2>/dev/null >> results/compiles-$interlines-$mode-$config.out

            # For debugging
            ./benchmark-$interlines.o $config $mode 1 0 1 2>/dev/null > results/assembly-$interlines-$mode-$config.out
        done
    done
done
