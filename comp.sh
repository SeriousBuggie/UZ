# fix for Ubuntu 12.04
export LIBRARY_PATH=/usr/lib/$(gcc -print-multiarch)/
export C_INCLUDE_PATH=/usr/include/$(gcc -print-multiarch)
export CPLUS_INCLUDE_PATH=/usr/include/$(gcc -print-multiarch)

rm divsufsort.o UZ.o uz 

# compile and output to this folder -> no linking yet!
gcc-2.95 -save-temps -c -D__USE_GNU -D_GNU_SOURCE -D_REENTRANT -D__LINUX_X86__ -fexceptions -O3 -fomit-frame-pointer -march=pentium -fPIC \
-DGPackage=UZ -Werror -I../Core/Inc \
./divsufsort.cpp ./UZ.cpp

# link with UT libs
gcc-2.95 -save-temps -lm -ldl -lpthread -o uz \
-Wl,-rpath,. \
-Wl,--dynamic-linker=/lib/ld-linux.so.2 \
-Wl,--eh-frame-hdr \
-Wl,--traditional-format \
UZ.o divsufsort.o Core.so

