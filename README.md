# FOEDAG <img src="./docs/source/overview/figures/osfpga_logo.png" width="200" align="right">

[![main](https://github.com/os-fpga/FOEDAG/actions/workflows/main.yml/badge.svg)](https://github.com/os-fpga/FOEDAG/actions/workflows/main.yml)
[![Documentation Status](https://readthedocs.org/projects/foedag/badge/?version=latest)](https://foedag.readthedocs.io/en/latest/?badge=latest)

FOEDAG denotes Qt-based Framework Open EDA Gui

## This fork

QuickLogic's fork of [os-fpga/FOEDAG](https://github.com/os-fpga/FOEDAG). It provides the compiler
front end for [aurora2](https://github.com/QL-Proprietary/aurora2), which consumes it as the
`foedag/foedag-gh` submodule. The badges above report upstream's CI, not this fork's.

- **Default branch is `develop`.** Open PRs against it.
- **QuickLogic-specific code is under `src/Compiler/`** — mainly `CompilerOpenFPGA_ql.cpp`, which
  drives the Aurora flow (synthesis, packing, place, route, STA, bitstream), and `QLDeviceManager`,
  which discovers device packages and reads their settings.
- **Author changes here, not in aurora2's nested `foedag/foedag-gh` checkout.** Merge to `develop`
  first, then bump aurora2's submodule pin to the merged commit.

## Documentation

FOEDAG's [full documentation](https://foedag.readthedocs.io/en/latest/) includes tutorials, tool options and contributor guidelines.

How a device's layout is chosen — `DEVICE_TYPE` and `DEVICE_TYPE_SETTINGS.LAYOUT_MODE` in the device
package's `config.json`, and the `custom_layout.yml` override — is documented in the aurora2 user
guide, `docs/public/aurora_user_guide/source/features/feature_auto_layout_generation.md`.

## Build instructions

Usually built through aurora2, which builds this submodule as part of its own `make`. To build
standalone, read [`INSTALL`](INSTALL.md) for more details

```bash
  make
or
  make debug
or
  make release_no_tcmalloc (For no tcmalloc)
  
make install (/usr/local/bin and /usr/local/lib/foedag by default which requires sudo privilege,
             use PREFIX= for alternative locations.)
```
