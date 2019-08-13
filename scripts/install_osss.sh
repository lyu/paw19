#!/usr/bin/env bash


export INSTALLPATH=$HOME/osss
export CFLAGS="-O2 -march=native"
export CXXFLAGS="-O2 -march=native"
export LDFLAGS="-s"


git clone https://github.com/openucx/ucx
cd ucx
git checkout acf0152
./autogen.sh
./contrib/configure-release --prefix=$INSTALLPATH --disable-static --enable-cma --enable-mt --with-avx --without-java --with-knem=/opt/knem-1.1.3.90mlnx1
make -j 8 && make install
cd ..
rm -rf ucx*


wget https://download.open-mpi.org/release/hwloc/v2.0/hwloc-2.0.4.tar.bz2
tar xf hwloc-2.0.4.tar.bz2
cd hwloc-2.0.4
./configure --prefix=$INSTALLPATH
make -j 8 && make install
cd ..
rm -rf hwloc*


wget https://github.com/pmix/pmix/releases/download/v3.1.3/pmix-3.1.3.tar.bz2
tar xf pmix-3.1.3.tar.bz2
cd pmix-3.1.3
./configure --prefix=$INSTALLPATH --disable-dlopen --with-hwloc=$INSTALLPATH --with-libevent=$HOME/.local
make -j 8 && make install
cd ..
rm -rf pmix*


wget https://github.com/pmix/prrte/releases/download/v1.0.0/prrte-1.0.0.tar.bz2
tar xf prrte-1.0.0.tar.bz2
cd prrte-1.0.0
./configure --prefix=$INSTALLPATH --with-hwloc=$INSTALLPATH --with-pmix=$INSTALLPATH --with-libevent=$HOME/.local --without-slurm --with-tm=/cm/shared/apps/torque/6.1.1
make -j 8 && make install
cd ..
rm -rf prrte*


git clone https://github.com/openshmem-org/osss-ucx
cd osss-ucx
git checkout 690f54d
./autogen.sh
./configure --prefix=$INSTALLPATH --disable-static --enable-threads --enable-aligned-addresses --with-pmix=$INSTALLPATH --with-ucx=$INSTALLPATH --with-heap-size=2048M
make -j 8 && make install
cd ..
rm -rf osss-ucx
