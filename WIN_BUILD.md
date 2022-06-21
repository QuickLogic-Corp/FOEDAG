
## Install MSYS2 by following all the steps as it is in https://www.msys2.org/

Run MSYS2 x64 shell in admin mode. You can launch cmd command window in admin mode and issue the below command
```
C:\msys64\msys2_shell.cmd -mingw64
```
In Win 11 the 64 bit executable resides in below path. Right click and run as administrator will launch the shell in admin mode.
(C:\Users\\<USER_NAME>\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\MSYS2 64bit)

Add the following path to Windows PATH variable.
```
C:\msys64\mingw64\bin
```
Add a new variable with variable name called 
```
Qt5_DIR
```
and variable value as 
```
/mingw64/lib/cmake/Qt5/
```
You can use the below link as a guide to set the variables.
https://www.computerhope.com/issues/ch000549.htm#windows11


## Install all the below mentioned packages
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
## It is recommended to create a new folder and clone into that folder and build.

## Clone and Initialize Submodules
```
git clone https://github.com/QuickLogic-Corp/FOEDAG.git
cd FOEDAG
git checkout winbuild
git submodule update --init --recursive
```
## Compile source codes

```
cmake -G "MSYS Makefiles" -DCMAKE_BUILD_TYPE=Release
make
```


## Install Aurora.

https://github.com/QuickLogic-Corp/edaSoftware/releases

Download windows sfx.exe from assets in the alpha.3 release (latest)

Double click the .exe, and set any location as the installation path. If C:\ as the path, then aurora will be extracted to C:\. C:\Aurora

In C:\Aurora\bin directory, all exes will be there, yosys, vpr etc.

Add the following path to Windows PATH variable.
```
<Your installation location>\Aurora\bin
```
You can use the below link as a guide to set the variables.
https://www.computerhope.com/issues/ch000549.htm#windows11

## Compile latest openfpga source code on Windows using MSYS2-Mingwx64 shell.
This step will generate latest libopenfpga.a, openfpga.exe, libvpr.a, vpr.exe.

Launch MSYS2-Mingwx64 shell

Download this cmake file from
https://github.com/QuickLogic-Corp/edaSoftware/blob/0d1f46701a3f07bb1f85cf3f8c3128bf3afae7f6/external_libs/openFPGA_vpr/CMakeLists.txt

Create a directory called openFPGAGit
cd to openFPGAGit

Copy the above downloaded cmake file into openFPGAGit folder

Follow the below commands as it is.
```
git clone https://github.com/LNIS-Projects/OpenFPGA.git
cmake -B build . -DVPR_USE_EZGL=off -G "MinGW Makefiles"
cd build/
mingw32-make.exe
```
Copy C:\openFPGAGit\build\OpenFPGA\openfpga\libopenfpga.a to C:\Aurora\bin. Replace.

Copy C:\openFPGAGit\build\OpenFPGA\openfpga\openfpga.exe to C:\Aurora\bin. Replace.

Copy C:\openFPGAGit\build\OpenFPGA\vpr\libvpr.a to C:\Aurora\bin. Replace.

Copy C:\openFPGAGit\build\OpenFPGA\vpr\vpr.exe to C:\Aurora\bin. Replace.



## Compile latest yosys source code on Windows using MSYS2-Mingwx64 shell.

This step will generate latest yosys.exe.

Launch MSYS2-Mingwx64 shell

Download this make file from
(Krishna will provide a link to this make file)

and patch file from
(Krishna will provide a link to this patch file)

Create a directory called yosysWinbuild

cd to yosysWinbuild

Copy the make file and the patch file to yosysWinbuild directory.

Follow the below command as it is.
```
make
```

Copy the contents of C:\yosysWinbuild\build\bin to C:\Aurora\bin. Replace all contents.

Copy the folder C:\yosysWinbuild\build\share\yosys\quicklogic\qlf_k6n10f to C:\Aurora\share\yosys\quicklogic. Replace all contents.


## Test counter_16bit example.
In the MSYS2 MINGWx64 shell in which FOEDAG was built, follow the below steps.

```
cd examples/counter_16bit/
../../bin/foedag.exe --batch --script counter_16bit.tcl --compiler ql
```
This will launch FOEDAG in batch mode and run the example.

Executing without --batch option like this
```
../../bin/foedag.exe --script counter_16bit.tcl --compiler ql
```
will launch FOEDAG in GUI mode and run the example.

In case you need to run synth step every time, delete FOEDAG\examples\counter_16bit\counter_16bit folder.
