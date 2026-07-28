#!/usr/bin/env bash

# The package list is designed for Ubuntu 24.04 LTS (Noble Numbat)
apt-get update
apt-get install -y \
    g++ gcc \
    tclsh \
    cmake \
    build-essential \
    google-perftools \
    libgoogle-perftools-dev \
    libunwind-dev \
    libtcmalloc-minimal4t64 \
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
