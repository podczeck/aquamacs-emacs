#!/bin/sh

cd ~/Nightly/Carbon22/
rm aquamacs-build.log
cd aquamacs-emacs.git
rm aquamacs-build.log
git checkout -f Aquamacs22 >>aquamacs-build.log
git pull origin Aquamacs22 >>aquamacs-build.log
./build-aquamacs.sh -lc aquamacs



