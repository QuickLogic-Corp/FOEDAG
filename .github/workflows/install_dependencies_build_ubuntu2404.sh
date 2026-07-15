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
    ninja-build
