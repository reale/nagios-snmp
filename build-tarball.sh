#!/bin/bash

dirname=`cat PROJECT`-`cat VERSION`
tarball=$dirname.tar.gz


# clean up build directory
cd src
./switch-debug-production --debug
make clean
./switch-debug-production --clean
cd ..

# don't pollute environment
./clean-all

# avoid tar's error message ``file changed as we read it''
touch $tarball

tar cfz $tarball --transform "s,^\./,$dirname/," --exclude=.svn --exclude=$tarball .
