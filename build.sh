#!/bin/sh

cd xpdf
make $@
cd ../tsundoku
make $@
cd ..
