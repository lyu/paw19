#!/usr/bin/env bash


export N_JOB_PTS=40
export N_PT_ITERS=4096
export N_NODES=32
export N_NUMAS_NODE=2
export N_CORES_NUMA=12

export SHMEM_SYMMETRIC_SIZE=2500M
export OMP_PROC_BIND=true

oshcxx -std=c++14 -Wall -Wextra -O3 -march=native -fopenmp mandelbrot.cpp -o man

for NN in $(seq 2 $N_NODES)
do
    export NNU=$(($NN * $N_NUMAS_NODE))
    export NC=$(($NN * $N_NUMAS_NODE * $N_CORES_NUMA))

    oshrun -n $NC --map-by core:span --rank-by core:span --bind-to core ./man -j $N_JOB_PTS -i $N_PT_ITERS -t 1 -p
    oshrun -n $NNU --map-by numa:span --bind-to numa ./man -j $N_JOB_PTS -i $N_PT_ITERS -t $N_CORES_NUMA
    oshrun -n $NNU --map-by numa:span --bind-to numa ./man -j $N_JOB_PTS -i $N_PT_ITERS -t $N_CORES_NUMA -p
done

rm man
