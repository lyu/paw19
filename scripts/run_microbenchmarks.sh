#!/usr/bin/env bash
#
# This script was used to run the AMO microbenchmarks and ping-pong microbenchmarks
# using all 48 cores on the two nodes, since they both use very small message sizes
# and so NUMA effect can be ignored
#
# To select different microbenchmarks, simply comment/uncomment the corresponding
# lines in the main function of microbenchmarks_mt.cpp & microbenchmarks_pure.cpp
#
# put/get tests use similar commands to run, but the hybrid versions are bound to a
# single NUMA domain and only use up to 12 threads, and the pure versions use up to
# 12 processes per node (also in the same socket)
#
# Example for running put test w/ 12 cores per node:
# oshrun -n 2 --map-by node:span --bind-to numa ./a.out 12 >> put_ctx
# oshrun -n 24 --map-by node:span --rank-by core:span --bind-to core ./a.out 12 >> put_pure

# This is super important for UCX
export OMP_PROC_BIND=true

oshcxx -std=c++14 -Wall -Wextra -fopenmp -march=native -O2 microbenchmarks_mt.cpp

oshrun -n 2 --map-by node:span --bind-to none ./a.out 1 > amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 2 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 4 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 6 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 8 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 10 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 12 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 14 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 16 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 18 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 20 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 22 >> amo_fetch
oshrun -n 2 --map-by node:span --bind-to none ./a.out 24 >> amo_fetch

oshcxx -std=c++14 -Wall -Wextra -fopenmp -march=native -O2 -DUSE_CTX microbenchmarks_mt.cpp

oshrun -n 2 --map-by node:span --bind-to none ./a.out 1 > amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 2 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 4 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 6 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 8 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 10 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 12 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 14 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 16 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 18 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 20 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 22 >> amo_fetch_ctx
oshrun -n 2 --map-by node:span --bind-to none ./a.out 24 >> amo_fetch_ctx

oshcxx -std=c++14 -Wall -Wextra -march=native -O2 microbenchmarks_pure.cpp

oshrun -n 2 --map-by node:span --rank-by core:span --bind-to core ./a.out 1 > amo_fetch_pure
oshrun -n 4 --map-by node:span --rank-by core:span --bind-to core ./a.out 2 >> amo_fetch_pure
oshrun -n 8 --map-by node:span --rank-by core:span --bind-to core ./a.out 4 >> amo_fetch_pure
oshrun -n 12 --map-by node:span --rank-by core:span --bind-to core ./a.out 6 >> amo_fetch_pure
oshrun -n 16 --map-by node:span --rank-by core:span --bind-to core ./a.out 8 >> amo_fetch_pure
oshrun -n 20 --map-by node:span --rank-by core:span --bind-to core ./a.out 10 >> amo_fetch_pure
oshrun -n 24 --map-by node:span --rank-by core:span --bind-to core ./a.out 12 >> amo_fetch_pure
oshrun -n 28 --map-by node:span --rank-by core:span --bind-to core ./a.out 14 >> amo_fetch_pure
oshrun -n 32 --map-by node:span --rank-by core:span --bind-to core ./a.out 16 >> amo_fetch_pure
oshrun -n 36 --map-by node:span --rank-by core:span --bind-to core ./a.out 18 >> amo_fetch_pure
oshrun -n 40 --map-by node:span --rank-by core:span --bind-to core ./a.out 20 >> amo_fetch_pure
oshrun -n 44 --map-by node:span --rank-by core:span --bind-to core ./a.out 22 >> amo_fetch_pure
oshrun -n 48 --map-by node:span --rank-by core:span --bind-to core ./a.out 24 >> amo_fetch_pure
