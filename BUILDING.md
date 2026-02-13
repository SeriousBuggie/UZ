# Building UZ from Source

UZ depends on the **Unreal Tournament (UT99) public source SDK** for its Core headers and shared library. This document explains how to set everything up on Linux and macOS.

## Prerequisites

| Requirement | Linux | macOS |
|---|---|---|
| C++ compiler | `g++` (GCC 7+) or `clang++` | Xcode Command Line Tools (`xcode-select --install`) |
| pthreads | included (glibc) | included |
| UT99 public source headers | see below | see below |
| UT99 `Core.so` / `Core.dylib` | see below | see below |

### Install compiler (Linux)

```bash
# Debian / Ubuntu
sudo apt install build-essential

# Fedora / RHEL
sudo dnf install gcc-c++ make

# Arch
sudo pacman -S base-devel
```

## Step 1: Get the Unreal Tournament SDK Headers

UZ uses headers from the UT99 public source release (`ut432pubsrc`). You need the `Core/Inc` directory that contains `Core.h`, `FCodec.h`, `FFileManagerLinux.h`, etc.

**Option A – OldUnreal 469 SDK (recommended)**

Clone or download the OldUnreal UT99 source from GitHub. It is actively maintained and builds with modern compilers:

```
https://github.com/OldUnreal/UnrealTournamentPatches
```

**Option B – Original ut432pubsrc**

The original Epic source release `ut432pubsrc.zip` can be found on various UT community sites and archives.

### Directory Layout

Place the UZ repository as a sibling of the `Core` directory from the SDK, so the directory tree looks like this:

```
parent_dir/
├── Core/
│   └── Inc/          ← SDK headers (Core.h, FCodec.h, …)
└── UZ/               ← this repository
    ├── UZ.cpp
    ├── divsufsort.cpp
    ├── comp.sh
    └── …
```

The build commands below use `-I../Core/Inc` to find the headers. Adjust the path if your layout differs.

## Step 2: Get the Core Shared Library

At link time, UZ needs the compiled Core library from a UT99 **Linux** (or macOS) installation.

- **Linux**: Copy `Core.so` from your UT99 Linux `System/` directory into the UZ source folder (or note the path for `-L`).
- **macOS**: You need a macOS-native `Core.dylib`. See the macOS-specific notes below.

## Step 3: Build (Linux)

From the `UZ/` directory:

```bash
# 1. Compile object files
g++ -c -O3 -fPIC \
    -D__LINUX_X86__ -D__USE_GNU -D_GNU_SOURCE -D_REENTRANT \
    -DGPackage=UZ \
    -I../Core/Inc \
    -Wno-narrowing -fpermissive \
    divsufsort.cpp UZ.cpp

# 2. Link
g++ -o uz UZ.o divsufsort.o \
    Core.so \
    -lm -ldl -lpthread \
    -Wl,-rpath,'$ORIGIN'
```

> **Note:** `-fpermissive` and `-Wno-narrowing` are typically needed because the UT SDK headers use coding patterns that modern compilers reject by default.

> **Note:** `-Wl,-rpath,'$ORIGIN'` tells the linker to look for `Core.so` in the same directory as the `uz` binary at runtime. Put `Core.so` next to the `uz` binary when deploying.

### One-Liner

```bash
g++ -O3 -fPIC -D__LINUX_X86__ -D__USE_GNU -D_GNU_SOURCE -D_REENTRANT -DGPackage=UZ -I../Core/Inc -Wno-narrowing -fpermissive -o uz divsufsort.cpp UZ.cpp Core.so -lm -ldl -lpthread -Wl,-rpath,'$ORIGIN'
```

## macOS-Specific Notes

Building on macOS requires a few source-level workarounds because the code uses some Linux-only headers and functions.

### Issues and Fixes

1. **`<sys/sysinfo.h>` does not exist on macOS.**
   Replace `#include <sys/sysinfo.h>` with a macOS alternative:
   ```cpp
   #ifdef __APPLE__
       #include <sys/sysctl.h>
   #else
       #include <sys/sysinfo.h>
   #endif
   ```

2. **`get_nprocs()` does not exist on macOS.**
   In the `main()` function, replace the thread count detection:
   ```cpp
   #ifdef __APPLE__
       ThreadsCount = sysconf(_SC_NPROCESSORS_ONLN);
   #else
       ThreadsCount = get_nprocs();
   #endif
   ```
   (Add `#include <unistd.h>` if not already included.)

3. **`<malloc.h>` does not exist on macOS.**
   Replace `#include <malloc.h>` with:
   ```cpp
   #ifdef __APPLE__
       #include <stdlib.h>
   #else
       #include <malloc.h>
   #endif
   ```
   (`malloc` and friends are declared in `<stdlib.h>` on macOS.)

4. **Core shared library.**
   On macOS you need `Core.dylib` instead of `Core.so`. This requires either a macOS-native UT build (e.g. from OldUnreal) or building Core from the SDK source for macOS.

5. **`-ldl` is not needed on macOS** (`dlopen` etc. are in libSystem).

### Build Command (macOS)

After applying the source fixes above:

```bash
clang++ -O3 -fPIC \
    -D__LINUX_X86__ -D__USE_GNU -D_GNU_SOURCE -D_REENTRANT \
    -DGPackage=UZ \
    -I../Core/Inc \
    -Wno-narrowing -fpermissive \
    -o uz divsufsort.cpp UZ.cpp \
    Core.dylib \
    -lpthread \
    -Wl,-rpath,'@executable_path'
```

> **Note:** The `-D__LINUX_X86__` flag is still used to select the POSIX/Linux code paths in the UT SDK headers (file manager, etc.). The OldUnreal SDK may provide a more appropriate `__APPLE__` code path; check its documentation.

## Troubleshooting

| Problem | Solution |
|---|---|
| `Core.h: No such file or directory` | Check that `-I../Core/Inc` points to the correct SDK path. |
| `undefined reference to …` (Core symbols) | Make sure `Core.so` is in the search path (same directory or `-L` flag). |
| `error: narrowing conversion` | Add `-Wno-narrowing`. |
| `error: invalid conversion` / old-style casts | Add `-fpermissive`. |
| `cannot open shared object file: Core.so` at runtime | Place `Core.so` next to the `uz` binary, or set `LD_LIBRARY_PATH`. |
| `sys/sysinfo.h: No such file or directory` (macOS) | Apply the macOS source fixes described above. |
| `malloc.h: No such file or directory` (macOS) | Apply the macOS source fixes described above. |
