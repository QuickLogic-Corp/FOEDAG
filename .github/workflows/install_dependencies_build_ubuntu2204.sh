#!/usr/bin/env bash

# The package list is designed for Ubuntu 22.04 LTS
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get update
apt-get install -y \
    g++-11 gcc-11 \
    tclsh \
    cmake \
    build-essential \
    google-perftools \
    libgoogle-perftools-dev \
    libunwind-dev \
    libtcmalloc-minimal4 \
    uuid-dev \
    lcov \
    valgrind \
    xorg \
    xvfb \
    yosys \
    automake \
    ninja-build
