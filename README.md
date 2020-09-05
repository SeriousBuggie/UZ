# UZ
Fast UZ compressor, drop-in replacement UCC.exe for compress/decompress unreal .uz files


    Usage:
        uz compress files_for_compress [newformat] [update] [buffer=N]
        uz decompress files_for_decompress

        newformat       Applies run-length encoding to the compressed files. This increases the compression rate.
        update          Only compress if the uz file does not exist, or if it is older than the corresponding source file.
        buffer=N        Limit BWT buffer to specified size. N is number, which can be in decimal or hexadecimal form (with prefix 0x). Bigger buffer - better but slower compression. Default limit: 0x2000
        
This tool produce fully compatible .uz file. No need ucc.exe.

Compressed faster in ~5 times.
