#!/usr/bin/env bash


export MESH_SIZE=3072
export N_ITERS=500
export N_CORES_NUMA=12

export SHMEM_SYMMETRIC_SIZE=1000M
export OMP_PROC_BIND=true

oshcxx -std=c++14 -Wall -Wextra -O2 -march=native -fopenmp -DUSE_DOUBLE halo3d.cpp -o halo3d.x
oshcxx -std=c++14 -Wall -Wextra -O2 -march=native -fopenmp -DUSE_DOUBLE -DUSE_CTX halo3d.cpp -o halo3d_ctx.x

oshrun -n 48 --map-by core:span --rank-by core:span --bind-to core ./halo3d.x -x 6 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t 1
oshrun -n 4 --map-by numa:span --bind-to numa ./halo3d.x -x 2 -y 2 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA
oshrun -n 4 --map-by numa:span --bind-to numa ./halo3d_ctx.x -x 2 -y 2 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA

oshrun -n 72 --map-by core:span --rank-by core:span --bind-to core ./halo3d.x -x 12 -y 6 -z 1 -I $N_ITERS -M $MESH_SIZE -t 1
oshrun -n 6 --map-by numa:span --bind-to numa ./halo3d.x -x 6 -y 1 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA
oshrun -n 6 --map-by numa:span --bind-to numa ./halo3d_ctx.x -x 6 -y 1 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA

oshrun -n 96 --map-by core:span --rank-by core:span --bind-to core ./halo3d.x -x 12 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t 1
oshrun -n 8 --map-by numa:span --bind-to numa ./halo3d.x -x 8 -y 1 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA
oshrun -n 8 --map-by numa:span --bind-to numa ./halo3d_ctx.x -x 8 -y 1 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA

oshrun -n 144 --map-by core:span --rank-by core:span --bind-to core ./halo3d.x -x 3 -y 48 -z 1 -I $N_ITERS -M $MESH_SIZE -t 1
oshrun -n 12 --map-by numa:span --bind-to numa ./halo3d.x -x 2 -y 6 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA
oshrun -n 12 --map-by numa:span --bind-to numa ./halo3d_ctx.x -x 2 -y 6 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA

oshrun -n 192 --map-by core:span --rank-by core:span --bind-to core ./halo3d.x -x 4 -y 48 -z 1 -I $N_ITERS -M $MESH_SIZE -t 1
oshrun -n 16 --map-by numa:span --bind-to numa ./halo3d.x -x 2 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA
oshrun -n 16 --map-by numa:span --bind-to numa ./halo3d_ctx.x -x 2 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA

oshrun -n 288 --map-by core:span --rank-by core:span --bind-to core ./halo3d.x -x 6 -y 48 -z 1 -I $N_ITERS -M $MESH_SIZE -t 1
oshrun -n 24 --map-by numa:span --bind-to numa ./halo3d.x -x 3 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA
oshrun -n 24 --map-by numa:span --bind-to numa ./halo3d_ctx.x -x 3 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA

oshrun -n 384 --map-by core:span --rank-by core:span --bind-to core ./halo3d.x -x 8 -y 48 -z 1 -I $N_ITERS -M $MESH_SIZE -t 1
oshrun -n 32 --map-by numa:span --bind-to numa ./halo3d.x -x 4 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA
oshrun -n 32 --map-by numa:span --bind-to numa ./halo3d_ctx.x -x 4 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA

oshrun -n 576 --map-by core:span --rank-by core:span --bind-to core ./halo3d.x -x 12 -y 48 -z 1 -I $N_ITERS -M $MESH_SIZE -t 1
oshrun -n 48 --map-by numa:span --bind-to numa ./halo3d.x -x 6 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA
oshrun -n 48 --map-by numa:span --bind-to numa ./halo3d_ctx.x -x 6 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA

oshrun -n 768 --map-by core:span --rank-by core:span --bind-to core ./halo3d.x -x 8 -y 96 -z 1 -I $N_ITERS -M $MESH_SIZE -t 1
oshrun -n 64 --map-by numa:span --bind-to numa ./halo3d.x -x 8 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA
oshrun -n 64 --map-by numa:span --bind-to numa ./halo3d_ctx.x -x 8 -y 8 -z 1 -I $N_ITERS -M $MESH_SIZE -t $N_CORES_NUMA

rm *.x
