# UZ
Fast UZ compressor, drop-in replacement UCC.exe for compress/decompress unreal .uz files


    Usage:
        uz compress files_for_compress [newformat] [update] [allowbad] [buffer=N]
        uz decompress files_for_decompress

        newformat       Applies run-length encoding to the compressed files. This increases the compression rate.
        update          Only compress if the uz file does not exist, or if it is older than the corresponding source file.
        allowbad        Allows the creation of .uz files that cause crash on clients below version 469.
        buffer=N        Limit the BWT buffer to the specified size. N is a number that can be in decimal or hexadecimal form (prefixed with 0x). Bigger buffer is better, but slower compression. Default limit: 0x40000. Maximal value: 0x40000.

This tool produce fully compatible .uz file. No need ucc.exe.

Compressed faster in 100-700 times.

bwtsort taken from https://sourceforge.net/projects/bwtcoder/

sais taken from https://sites.google.com/site/yuta256/sais

libdivsufsort-lite taken from https://code.google.com/archive/p/libdivsufsort

Thanks Mc.Gugi for uzLib (not used but very helpful).

Thanks Eternity for investigate why sais produce different results.

For use on Windows need install latest Runtime for Visual Studio: 

https://support.microsoft.com/en-us/topic/the-latest-supported-visual-c-downloads-2647da03-1eea-4433-9aff-95f26a218cc0
