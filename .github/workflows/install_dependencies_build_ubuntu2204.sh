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
    ninja-build \
    libxcb-cursor0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-shape0 \
    libxcb-xinerama0 \
    libxcb-xkb1 \
    libxkbcommon0 \
    libxkbcommon-x11-0 \
    libgl1 \
    libfontconfig1 \
    libdbus-1-3
