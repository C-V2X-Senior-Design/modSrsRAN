#!/usr/bin/bash

cd build
cmake -g ../  # ADDED -g compiler flag to neable gdb usage
make
sudo make install
