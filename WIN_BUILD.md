
### Install MSYS2 by following all the steps as it is in https://www.msys2.org/

### Run MSYS2 x64 shell in admin mode. (C:\Users\somes\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\MSYS2 64bit)
### Add a path variable C:\msys64\mingw64\bin (Use advanced system settings and edit path variable)

### Install all the below mentioned packages
```
pacman -S git
pacman -S mingw-w64-x86_64-cmake
pacman -S mingw-w64-x86_64-qt5-base-debug
pacman -S mingw-w64-x86_64-qt5
pacman -S mingw-w64-x86_64-swig
pacman -S mingw-w64-x86_64-qt5-declarative-debug
pacman -S mingw-w64-x86_64-tcl
pacman -S mingw-w64-x86_64-zlib
```

### Clone and Initialize Submodules
```
git clone https://github.com/os-fpga/FOEDAG.git
cd FOEDAG
git checkout winbuild
git submodule update --init --recursive
```
### Compile source codes

```
cmake -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
make
```
