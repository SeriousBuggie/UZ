#if WIN32
	#include <windows.h>
#else
	#include <errno.h>
	#include <sys/stat.h>
#endif
#include <malloc.h>
#include <stdio.h>
#include <algorithm>

#include <Core.h>
#include <FCodec.h>
//#include <Engine.h>

#include "bwtsort.h"

INT GFilesOpen, GFilesOpened;

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// General.
#if _MSC_VER
	extern "C" {HINSTANCE hInstance;}
#endif
extern "C" {TCHAR GPackage[64]=TEXT("UCC");}

// Log.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// Error.
#include "FOutputDeviceAnsiError.h"
FOutputDeviceAnsiError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;

// File manager.
#if WIN32
	#include "FFileManagerWindows.h"
	FFileManagerWindows FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#elif __PSX2_EE__
	#include "FFileManagerPSX2.h"
	FFileManagerPSX2 FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#elif __LINUX_X86__
	#include "FFileManagerLinux.h"
	FFileManagerLinux FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#else
	#include "FFileManagerAnsi.h"
	FFileManagerAnsi FileManager;
#endif

INT ThreadsCount;

/*-----------------------------------------------------------------------------
	Burrows-Wheeler inspired data compressor.
-----------------------------------------------------------------------------*/
// use https://sourceforge.net/projects/bwtcoder/files/bwtcoder/preliminary-2/

#define MAX_THREADS 32

#define SHOW_PROGRESS

typedef struct {
	BOOL Encode;
    TArray<BYTE> CompressBufferArray;
	BYTE* CompressBuffer;
    INT CompressLength;
	INT First, Last;
	TArray<INT>  Temp_;
	INT*  Temp;
	TArray<BYTE>  BufOut_;
	BYTE* BufOut;
} BWTData;

DWORD WINAPI BWTThread( LPVOID lpParam ) {
	BWTData* Data = (BWTData*)lpParam;
	if (Data->Encode) {
		ThreadData td;
		KeyPrefix* CompressPos = bwtsort(&td, Data->CompressBuffer, Data->CompressLength);
		CompressPos[Data->CompressLength].offset = Data->CompressLength;
		Data->First=0, Data->Last=0;
		for(INT i=0; i<Data->CompressLength+1; i++ ) {
			INT pos = CompressPos[i];
			if( pos==1 ) Data->First = i;
			else if( pos==0 ) Data->Last = i;
			Data->BufOut[i] = Data->CompressBuffer[pos?pos-1:0];
		}
		free(CompressPos);
	} else {
		INT DecompressCount[256+1], RunningTotal[256+1];
		memset(&DecompressCount[0], 0, sizeof(DecompressCount));
		for( INT i=0; i<Data->CompressLength; i++ )
			DecompressCount[ i!=Data->Last ? Data->CompressBuffer[i] : 256 ]++;
		INT Sum = 0;
		for( INT i=0; i<257; i++ )
		{
			RunningTotal[i] = Sum;
			Sum += DecompressCount[i];
			DecompressCount[i] = 0;
		}
		for( INT i=0; i<Data->CompressLength; i++ )
		{
			INT Index = i!=Data->Last ? Data->CompressBuffer[i] : 256;
			Data->Temp[RunningTotal[Index] + DecompressCount[Index]++] = i;
		}
		for( INT i=Data->First,j=0 ; j<Data->CompressLength-1; i=Data->Temp[i],j++ )
			Data->BufOut[j] = Data->CompressBuffer[i];
	}
	return 0;
}

INT RealBufferSize = 0x40000;

class FCodecBWT_fast : public FCodec
{
private:
	enum {MAX_BUFFER_SIZE=0x40000}; /* Hand tuning suggests this is an ideal size */
public:
	UBOOL Encode( FArchive& In, FArchive& Out )
	{
		guard(FCodecBWT::Encode);

		#ifdef SHOW_PROGRESS			
			Warn.LocalPrint(TEXT("0%"));
			FString Progress;
		#endif

		HANDLE  hThreadArray[MAX_THREADS]; 
		BWTData Data[MAX_THREADS];
		for (INT t = 0; t < ThreadsCount; t++) {
			Data[t].Encode = true;
			Data[t].CompressBufferArray.Add(MAX_BUFFER_SIZE);
			Data[t].CompressBuffer = &Data[t].CompressBufferArray(0);
			Data[t].BufOut_.Add(MAX_BUFFER_SIZE + 1);
			Data[t].BufOut = &Data[t].BufOut_(0);
		}

		while( !In.AtEnd() )
		{
			INT Threads = ThreadsCount;
			for (INT t = 0; t < ThreadsCount; t++) {
				Data[t].CompressLength = Min<INT>( In.TotalSize()-In.Tell(), MAX_BUFFER_SIZE );
				Data[t].CompressLength = Min<INT>( Data[t].CompressLength, RealBufferSize ); // reduce buffer for avoid slow down
				if (Data[t].CompressLength <= 0) {
					Threads = t;
					break;
				}
				In.Serialize( Data[t].CompressBuffer, Data[t].CompressLength );

				hThreadArray[t] = CreateThread(NULL, 0, BWTThread, &Data[t], 0, NULL);
			}

			WaitForMultipleObjects(Threads, hThreadArray, TRUE, INFINITE);

			for (INT t = 0; t < Threads; t++) {
				CloseHandle(hThreadArray[t]);
				Out << Data[t].CompressLength << Data[t].First << Data[t].Last;
				Out.Serialize(Data[t].BufOut, Data[t].CompressLength+1);
			}

			#ifdef SHOW_PROGRESS				
				Warn.LocalPrint(*FString::Printf(
					#ifdef DBG
						TEXT("\n")
					#else
						TEXT("\r")
					#endif
					TEXT("%.3f%%"), 100.f*In.Tell()/In.TotalSize()));
			#endif
		}
		#ifdef SHOW_PROGRESS
			Warn.LocalPrint(TEXT("\r"));
		#endif

		return 0;
		unguard;
	}
	UBOOL Decode( FArchive& In, FArchive& Out )
	{
		guard(FCodecBWT::Decode);

		HANDLE  hThreadArray[MAX_THREADS]; 
		BWTData Data[MAX_THREADS];
		for (INT t = 0; t < ThreadsCount; t++) {
			Data[t].Encode = false;
			Data[t].CompressBufferArray.Add(MAX_BUFFER_SIZE + 1);
			Data[t].CompressBuffer = &Data[t].CompressBufferArray(0);
			Data[t].Temp_.Add(MAX_BUFFER_SIZE + 1);
			Data[t].Temp = &Data[t].Temp_(0);
			Data[t].BufOut_.Add(MAX_BUFFER_SIZE);
			Data[t].BufOut = &Data[t].BufOut_(0);
		}

		while( !In.AtEnd() )
		{
			INT Threads = ThreadsCount;
			for (INT t = 0; t < ThreadsCount; t++) {
				if (In.AtEnd()) {
					Threads = t;
					break;
				}
				In << Data[t].CompressLength << Data[t].First << Data[t].Last;
				check(Data[t].CompressLength<=MAX_BUFFER_SIZE+1);
				check(Data[t].CompressLength<=In.TotalSize()-In.Tell());
				In.Serialize( &Data[t].CompressBuffer[0], ++Data[t].CompressLength );

				hThreadArray[t] = CreateThread(NULL, 0, BWTThread, &Data[t], 0, NULL);
			}

			WaitForMultipleObjects(Threads, hThreadArray, TRUE, INFINITE);

			for (INT t = 0; t < Threads; t++) {
				CloseHandle(hThreadArray[t]);
				Out.Serialize(Data[t].BufOut, Data[t].CompressLength-1);
			}
		}
		return 1;
		unguard;
	}
};

#define BUF_SIZE 0x10000

/*-----------------------------------------------------------------------------
	RLE compressor.
-----------------------------------------------------------------------------*/

class FCodecRLE_fast : public FCodec
{
private:
	enum {RLE_LEAD=5};
	inline void EncodeEmitRun( FArchive& Out, BYTE Char, BYTE Count )
	{
		for( INT Down=Min<INT>(Count,RLE_LEAD); Down>0; Down-- )
			Out << Char;
		if( Count>=RLE_LEAD )
			Out << Count;
	}
public:
	UBOOL Encode( FArchive& In, FArchive& Out )
	{
		guard(FCodecRLE::Encode);
		BYTE PrevChar=0, PrevCount=0, BufIn[BUF_SIZE];
		TArray<BYTE>  BufOut_(BUF_SIZE*6/5 + 10);
		BYTE* BufOut = &BufOut_(0);
		INT Length = In.TotalSize();
		while( Length > 0 )
		{
			INT BufLength = Min(Length, BUF_SIZE);
			In.Serialize( BufIn, BufLength );
			INT OutLength = 0;
			for( INT j=0; j<BufLength; j++ )
			{
				BYTE B = BufIn[j];
				if( B!=PrevChar || PrevCount==255 )
				{
					if (PrevCount == 1) {
						BufOut[OutLength++] = PrevChar;
					} else {
						for( INT End=OutLength + Min<INT>(PrevCount,RLE_LEAD); OutLength < End;)
							BufOut[OutLength++] = PrevChar;
						if( PrevCount>=RLE_LEAD )
							BufOut[OutLength++] = PrevCount;
					}
					PrevChar  = B;
					PrevCount = 1;
				} else PrevCount++;
			}
			Out.Serialize( BufOut, OutLength );
			Length -= BufLength;
		}
		EncodeEmitRun( Out, PrevChar, PrevCount );
		return 0;
		unguard;
	}
	UBOOL Decode( FArchive& In, FArchive& Out )
	{
		guard(FCodecRLE::Decode);
		INT Count=0;
		BYTE PrevChar=0, BufIn[BUF_SIZE];
		TArray<BYTE>  BufOut_(BUF_SIZE*255/(RLE_LEAD+1) + 10);
		BYTE* BufOut = &BufOut_(0);
		INT Length = In.TotalSize();
		while( Length > 0 )
		{
			INT BufLength = Min(Length, BUF_SIZE);
			In.Serialize( BufIn, BufLength );
			INT OutLength = 0;
			for( INT j=0; j<BufLength; j++ )
			{
				BYTE B = BufIn[j];
				BufOut[OutLength++] = B;
				if( B!=PrevChar )
				{
					PrevChar = B;
					Count    = 1;
				}
				else if( ++Count==RLE_LEAD )
				{
					BYTE C;
					if (j == BufLength - 1) {
						In << C;
						Length--;
					} else {
						C = BufIn[++j];
					}
					check(C>=2);
					C -= RLE_LEAD;
					memset(&BufOut[OutLength], B, C);					
					OutLength += C;
					Count = 0;
				}
			}
			Out.Serialize( BufOut, OutLength );
			Length -= BufLength;
		}
		return 1;
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	Huffman codec.
-----------------------------------------------------------------------------*/

static BYTE GShift[8]={0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};

class FCodecHuffman_fast : public FCodec
{
private:
	struct FHuffman
	{
		INT Ch, Count;
		FHuffman* Child[2];
		TArray<BYTE> Bits;
		FHuffman( INT InCh = -1, INT InCount = 0 )
			: Ch(InCh), Count(InCount), Child()
		{
		}
		~FHuffman()
		{
			if (Child[0])
				for( INT i=0; i<2; i++ )
					delete Child[i];
		}
		void PrependBit( BYTE B )
		{
			Bits.Insert( 0 );
			Bits(0) = B;
			if (Child[0])
				for( INT i=0; i<2; i++ )
					Child[i]->PrependBit( B );
		}
		void WriteTable( FBitWriter& Writer )
		{
			if( Child[0] ) {
				Writer.WriteBit( 1 );
				for( INT i=0; i<2; i++ )
					Child[i]->WriteTable( Writer );
			}
			else
			{
				Writer.WriteBit( 0 );
				BYTE B = Ch;
				Writer << B;
			}
		}
		void ReadTable( FBitReader& Reader )
		{
			if( Reader.ReadBit() )
			{
				for( INT i=0; i<2; i++ )
				{
					FHuffman* Huffman = new FHuffman();
					Child[ i ] = Huffman;
					Huffman->ReadTable( Reader );
				}
			}
			else Ch = Arctor<BYTE>( Reader );
		}
	};
	static QSORT_RETURN CDECL CompareHuffman( const FHuffman** A, const FHuffman** B )
	{
		return (*B)->Count - (*A)->Count;
	}
public:
	UBOOL Encode( FArchive& In, FArchive& Out )
	{
		guard(FCodecHuffman::Encode);
		BYTE BufIn[BUF_SIZE];
		INT SavedPos = In.Tell();
		INT Total=In.TotalSize()-In.Tell();
		Out << Total;

		INT Counts[256] = {0};
		for( INT Length=Total; Length>0;) {
			INT BufLength = Min(Length, BUF_SIZE);
			In.Serialize( BufIn, BufLength );
			for( INT j=0; j<BufLength; j++ )
				Counts[BufIn[j]]++;
			Length -= BufLength;
		}
		In.Seek( SavedPos );

		// Compute character frequencies.
		TArray<FHuffman*> Huff(256);
		for( INT i=0; i<256; i++ )
			Huff(i) = new FHuffman(i, Counts[i]);
		TArray<FHuffman*> Index = Huff;

		// Build compression table.
		while( Huff.Num()>1 && Huff.Last()->Count==0 )
			delete Huff.Pop();
		INT BitCount = Huff.Num()*(8+1);
		while( Huff.Num()>1 )
		{
			FHuffman* Node  = new FHuffman();
			for( INT i=0; i<2; i++ )
			{
				FHuffman* Huffman = Huff.Pop();
				Node->Child[i] = Huffman;
				Huffman->PrependBit(i);
				Node->Count += Huffman->Count;
			}
			INT i, N = Huff.Num();
			for( i=0; i<N; i++ )
				if( Huff(i)->Count < Node->Count )
					break;
			Huff.Insert( i );
			Huff( i ) = Node;
			BitCount++;
		}
		FHuffman* Root = Huff.Pop();

		// Calc stats.
		for( INT i=0; i<256; i++ ) {
			INT Count = Counts[i];
			if (Count == 0) continue;
			BitCount += Index(i)->Bits.Num()*Count;
		}

		// Save table and bitstream.
		FBitWriter Writer( BitCount );
		Root->WriteTable( Writer );
		INT Pos = Writer.GetNumBits();
		BYTE* Data = Writer.GetData();
		for( INT Length=Total; Length>0;) {
			INT BufLength = Min(Length, BUF_SIZE);
			In.Serialize( BufIn, BufLength );
			for( INT j=0; j<BufLength; j++ ) {
				FHuffman* P = Index(BufIn[j]);
				for( INT i=0, N=P->Bits.Num(); i<N; i++ ) {
					if (P->Bits(i))
						Data[Pos>>3] |= GShift[Pos&7];
					Pos++;
				}
			}
			Length -= BufLength;
		}
		check(!Writer.IsError());
		check(Pos==BitCount);
		Out.Serialize( Data, (Pos + 7)>>3 );

		// Finish up.
		delete Root;
		return 0;

		unguard;
	}
	UBOOL Decode( FArchive& In, FArchive& Out )
	{
		guard(FCodecHuffman::Decode);
		INT Total;
		In << Total;
		TArray<BYTE> InArray( In.TotalSize()-In.Tell() );
		In.Serialize( &InArray(0), InArray.Num() );
		FBitReader Reader( &InArray(0), InArray.Num()*8 );
		FHuffman Root(-1);
		Root.ReadTable( Reader );
		INT Pos = Reader.GetPosBits();
		BYTE* Data = Reader.GetData();
		INT TotalBits = Reader.GetNumBits();
		TArray<BYTE> BufOut_(Total);
		BYTE* BufOut = &BufOut_(0);
		INT OutLength = 0;
		while( OutLength < Total )
		{	
			FHuffman* Node = &Root;
			while( Node->Ch==-1 ) {
				Node = Node->Child[(Data[Pos>>3] & GShift[Pos&7]) != 0];
				Pos++;
			}
			BufOut[OutLength++] = Node->Ch;
		}
		InArray.Empty();
		Out.Serialize( BufOut, OutLength );
		check(TotalBits - Pos >= 0);
		return 1;
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	Move-to-front encoder.
-----------------------------------------------------------------------------*/

class FCodecMTF_fast : public FCodec
{
public:
	UBOOL Encode( FArchive& In, FArchive& Out )
	{
		guard(FCodecMTF::Encode);
		BYTE List[256], BufIn[BUF_SIZE], BufOut[BUF_SIZE];
		INT Length = In.TotalSize();
		for( INT i=0; i<256; i++ ) List[i] = i;
		while( Length > 0 )
		{
			INT BufLength = Min(Length, BUF_SIZE);
			In.Serialize( BufIn, BufLength );
			for( INT j=0; j<BufLength; j++ )
			{
				BYTE B = BufIn[j];
				BYTE i = (BYTE)((BYTE*)memchr(&List[0], B, 256) - &List[0]);
				BufOut[j] = i;
				memmove(&List[1], &List[0], i);
				List[0] = B;
			}
			Out.Serialize( BufOut, BufLength );
			Length -= BufLength;
		}
		return 0;
		unguard;
	}
	UBOOL Decode( FArchive& In, FArchive& Out )
	{
		guard(FCodecMTF::Decode);
		BYTE List[256], BufIn[BUF_SIZE], BufOut[BUF_SIZE];
		INT Length = In.TotalSize();
		for( INT i=0; i<256; i++ ) List[i] = i;
		while( Length > 0 )
		{
			INT BufLength = Min(Length, BUF_SIZE);
			In.Serialize( BufIn, BufLength );
			for( INT j=0; j<BufLength; j++ )
			{
				BYTE B = BufIn[j];
				BYTE C = List[B];
				BufOut[j] = C;
				memmove(&List[1], &List[0], B);
				List[0] = C;
			}
			Out.Serialize( BufOut, BufLength );
			Length -= BufLength;
		}
		return 1;
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	Main.
-----------------------------------------------------------------------------*/

int main( int argc, char* argv[] ) {
	#if !_MSC_VER
		__Context::StaticInit();
	#endif

	INT ErrorLevel = 0;
	GIsStarted     = 1;
#ifndef _DEBUG
	try
#endif
	{
		GIsGuarded = 1;

		#if !_MSC_VER
		// Set module name.
		appStrcpy( GModule, argv[0] );
		#endif

		GLog = &Log;
		GError = &Error;
		GWarn = &Warn;
		GFileManager = &FileManager;

		GMalloc = &Malloc;
		GMalloc->Init();

		// Switch into executable's directory.
		GFileManager->Init(1);
		GFileManager->SetDefaultDirectory( appBaseDir() );

		// Memory initalization.
		GMem.Init( 65536 );

		FString app = appFromAnsi(argv[0]);
		int delim = Max(app.InStr(TEXT("/"), 1), app.InStr(TEXT("\\"), 1));
		if (delim >= 0) {
			app = app.Right(app.Len() - delim - 1);
		}
		FString Token = argc>1 ? appFromAnsi(argv[1]) : TEXT("");
		GIsClient = GIsServer = GIsEditor = GIsScriptable = 1;
		GLazyLoad = 0;
		UBOOL Help = 0;
		DWORD LoadFlags = LOAD_NoWarn | LOAD_Quiet;
		
		if (Token == TEXT("") || Token == TEXT("HELP")) {
			Warn.Logf(TEXT("Usage:\n")
				TEXT("\t%s compress files_for_compress [newformat] [update] [allowbad] [buffer=N]\n")
				TEXT("\t%s decompress files_for_decompress\n\n")
				TEXT("\tnewformat\tApplies run-length encoding to the compressed files. This increases the compression rate.\n")
				TEXT("\tupdate\t\tOnly compress if the uz file does not exist, or if it is older than the corresponding source file.\n")
				TEXT("\tallowbad\tAllows the creation of .uz files that cause crash on clients below version 469.\n")
				TEXT("\tbuffer=N\tLimit the BWT buffer to the specified size. N is a number that can be in decimal or hexadecimal form (prefixed with 0x). Bigger buffer is better, but slower compression. Default limit: 0x%X. Maximal value: 0x40000."),
				*app, 
				*app,
				RealBufferSize);
		} else {
			{
				SYSTEM_INFO sysinfo;
				GetSystemInfo(&sysinfo);
				ThreadsCount = Max<INT>(1, Min<INT>(sysinfo.dwNumberOfProcessors, MAX_THREADS));
				//ThreadsCount = 1; // dbg
				Warn.Logf(TEXT("Used %d threads."), ThreadsCount);
			}

			int last = argc - 1;
			FString CFile;
			FString UFile;
			FCodecFull Codec[2];

			Codec[0].AddCodec(new FCodecRLE_fast);
			Codec[0].AddCodec(new FCodecBWT_fast);
			Codec[0].AddCodec(new FCodecMTF_fast);
			Codec[0].AddCodec(new FCodecHuffman_fast);

			Codec[1].AddCodec(new FCodecRLE_fast);
			Codec[1].AddCodec(new FCodecBWT_fast);
			Codec[1].AddCodec(new FCodecMTF_fast);
			Codec[1].AddCodec(new FCodecRLE_fast);
			Codec[1].AddCodec(new FCodecHuffman_fast);

			if (Token == TEXT("COMPRESS")) {
				bool newformat = false;
				bool update = false;
				bool extended = false;
				bool AllowBad = false;
				while (true) {
					Token = appFromAnsi(argv[last]);
					if (Token == TEXT("NEWFORMAT")) {
						newformat = true;
					} else if (Token == TEXT("UPDATE")) {
						update = true;
					} else if (Token == TEXT("ALLOWBAD")) {
						AllowBad = true;
					} else if (Token.Left(7) == TEXT("BUFFER=")) {
						TCHAR** End = NULL;
						INT Size = wcstol(*Token + 7, End, 0);
						if (Size >= 10) {
							Warn.Logf(TEXT("The BWT buffer size is limited to %i (0x%X) bytes"), Size, Size);
							RealBufferSize = Size;
							extended = true;
						} else {
							Warn.Logf(TEXT("Can't use buffer size '%s'"), *Token + 7);
						}
					} else break;
					last--;
				}				
				for (int i = 2; i <= last; i++) {
					UFile = appFromAnsi(argv[i]);
					CFile = UFile + TEXT(".uz");					
					INT USize = GFileManager->FileSize(*UFile);
					Warn.Logf(TEXT("Compressing %s (%i bytes)"), *UFile, USize);

					if (update && GFileManager->GetGlobalTime(*UFile) <= GFileManager->GetGlobalTime(*CFile)) {
						Warn.Logf(TEXT("Compression skipped -> %s is already up to date"), *CFile);
						continue;
					}
					
					FArchive* UFileAr = GFileManager->CreateFileReader(*UFile);
					if (!UFileAr) {
						Warn.Logf(TEXT("Source %s not found"), *UFile);
						continue;
					}
					FArchive* CFileAr = GFileManager->CreateFileWriter(*CFile);
					if (!CFileAr) {
						Warn.Logf(TEXT("Could not create %s"), *CFile);
						delete UFileAr;
						continue;
					}
					INT Signature = newformat ? 5678 : 1234;
					FString OrigFilename;
					OrigFilename = UFile;
					int delim = Max(UFile.InStr(TEXT("/"), 1), UFile.InStr(TEXT("\\"), 1));
					if (delim >= 0) {
						OrigFilename = UFile.Right(UFile.Len() - delim - 1);
					}
					*CFileAr << Signature;
					*CFileAr << OrigFilename;
					Codec[newformat].Encode(*UFileAr, *CFileAr);
					delete UFileAr;
					delete CFileAr;
					INT CSize = GFileManager->FileSize(*CFile);
					if (CSize > USize && !AllowBad) {
						Warn.Logf(TEXT("Skipped: This .uz file cause crash on clients below 469. Use the 'allowbad' parameter to create this .uz file anyway.\n")
							TEXT("Such files should be the last in the ServerPackages list to avoid speed drops for all subsequent files on older clients."));
						continue;
					}
					FLOAT Rate = 100.f*CSize/USize;					
					if (extended) {
						Warn.Logf(TEXT("Compressed %s -> %s (%.3f%%)"), *UFile, *CFile, Rate);
					} else {
						Warn.Logf(TEXT("Compressed %s -> %s (%i%%)"), *UFile, *CFile, (INT)(Rate + 0.5f));
					}
				}
			} else if (Token == TEXT("DECOMPRESS")) {
				for (int i = 2; i <= last; i++) {
					CFile = appFromAnsi(argv[i]);
					if (CFile.Right(3) == TEXT(".UZ")) {
						UFile = CFile.Left(CFile.Len() - 3);
					} else {
						UFile = CFile + TEXT(".u");
					}
					FArchive* CFileAr = GFileManager->CreateFileReader(*CFile);
					if (!CFileAr) {
						Warn.Logf(TEXT("Source %s not found"), *CFile);
						continue;
					}
					INT Signature;
					FString OrigFilename;
					*CFileAr << Signature;
					bool newformat = false;
					if (Signature == 1234) {
					} else if (Signature == 5678) {
						newformat = true;
					} else {
						Warn.Logf(TEXT("Unknown signature %i (must be 1234 or 5768) in %s"), Signature, *CFile);
						delete CFileAr;
						continue;
					}
					*CFileAr << OrigFilename;
					FArchive* UFileAr = GFileManager->CreateFileWriter( *UFile );
					if (!UFileAr) {
						Warn.Logf(TEXT("Could not create %s"), *UFile);
						delete CFileAr;
						continue;
					}
					Codec[newformat].Decode(*CFileAr, *UFileAr);
					delete CFileAr;
					delete UFileAr;					
					Warn.Logf(TEXT("Decompressed %s -> %s"), *CFile, *UFile);
				}
			} else {
				Warn.Logf( TEXT("Unknown command '%s'. Try '%s help'."), *Token, *app);
			}
		}
		GMem.Exit();
		GIsGuarded = 0;
	}
#ifndef _DEBUG
	catch( ... )
	{
		// Crashed.
		ErrorLevel = 1;
		GIsGuarded = 0;
		Error.HandleError();
	}
#endif
	GIsStarted = 0;
	return ErrorLevel;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
