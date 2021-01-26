#!/bin/bash

if [ `id -u` -ne 0 ]
then
	echo 'You must be root!'
	exit 1
fi

PREFIX=`cat PROJECT`-`cat VERSION`


dirname=.nagios-tarball
tarball=$PREFIX-nagios.tar.gz

PREFIX_DIR=$dirname script/install.sh

cd $dirname
tar cfzp ../$tarball --same-owner .
cd - >/dev/null

rm -fr $dirname

echo Generating setperm script ...
./make-setperm.sh $tarball > $PREFIX-setperm.sh
chmod a+x $PREFIX-setperm.sh
