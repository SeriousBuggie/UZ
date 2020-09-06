# UZ
Fast UZ compressor, drop-in replacement UCC.exe for compress/decompress unreal .uz files


    Usage:
        uz compress files_for_compress [newformat] [update] [allowbad] [buffer=N]
        uz decompress files_for_decompress

        newformat       Applies run-length encoding to the compressed files. This increases the compression rate.
        update          Only compress if the uz file does not exist, or if it is older than the corresponding source file.
        allowbad        Allows the creation of .uz files that cause crash on clients below version 469.
        buffer=N        Limit the BWT buffer to the specified size. N is a number that can be in decimal or hexadecimal form (prefixed with 0x). Bigger buffer is better, but slower compression. Default limit: 0x2000. Maximal value: 0x40000.
        
This tool produce fully compatible .uz file. No need ucc.exe.

Compressed faster in ~5 times.
