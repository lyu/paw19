#!/usr/bin/env bash


export N_KEYS=100000
export N_ITERS=100
export N_NODES=32
export N_NUMAS_NODE=2
export N_CORES_NUMA=12

export SHMEM_SYMMETRIC_SIZE=500M
export OMP_PROC_BIND=true

oshcxx -std=c++14 -Wall -Wextra -O2 -march=native -fopenmp isx.cpp -o isx

for NN in $(seq 2 $N_NODES)
do
    export NNU=$(($NN * $N_NUMAS_NODE))
    export NC=$(($NN * $N_NUMAS_NODE * $N_CORES_NUMA))

    oshrun -n $NC --map-by core:span --rank-by core:span --bind-to core ./isx -i $N_ITERS -w $N_KEYS -t 1 -n -p
    oshrun -n $NNU --map-by numa:span --bind-to numa ./isx -i $N_ITERS -w $N_KEYS -t $N_CORES_NUMA -n
    oshrun -n $NNU --map-by numa:span --bind-to numa ./isx -i $N_ITERS -w $N_KEYS -t $N_CORES_NUMA -n -p
done

rm isx
