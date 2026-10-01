# Host compile check

`ps3toolchain` needs Linux or macOS, so it cannot be built on this machine.
This directory exists so the sources can still be type-checked by any host C
compiler using stub declarations of the PSL1GHT and SDL APIs.

It proves the code compiles and that the modules agree on signatures. It does
**not** prove anything about the hardware: no PowerPC code is generated, and no
LV2 syscall behaviour is exercised.

## With MSVC

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
check.bat
```

## With gcc / clang

```sh
./check.sh
```

## Building the real thing

Use ps3dev/PSL1GHT (not the HACKERCHANNEL fork — its header layout is
different), on Linux or in a container:

```sh
export PS3DEV=/usr/local/ps3dev
export PSL1GHT=$PS3DEV/psl1ght
make
```