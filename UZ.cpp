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

#define COMMENT SLASH(/)
#define SLASH(s) /##s

//#define delete COMMENT delete

/*-----------------------------------------------------------------------------
	Burrows-Wheeler inspired data compressor.
-----------------------------------------------------------------------------*/

#define USE_qsort 0 // AS UCC does
#define USE_sort 1 // use qsort mixed with heapsort after some depth, which slower from qsort
#define USE_stable_sort 2 // use merge sort, which faster from qsort
#define USE_bwtsort 3 // use https://sourceforge.net/projects/bwtcoder/files/bwtcoder/preliminary-2/ - best

#define USE_SORT USE_bwtsort 

#define SHOW_PROGRESS

//#define DBG

#ifdef DBG
	class FMeasure {
	public:
		INT P1, P2;
		FMeasure();
		~FMeasure();
	};
#endif

INT RealBufferSize = 0x40000;

class FCodecBWT_fast : public FCodec
{
private:
	enum {MAX_BUFFER_SIZE=0x40000}; /* Hand tuning suggests this is an ideal size */
	static BYTE* CompressBuffer;
	static INT CompressLength;
	#if (USE_SORT == USE_stable_sort || USE_SORT == USE_sort)
		static bool ClampedBufferCompare2( const INT P1, const INT P2 )
		{
			guardSlow(FCodecBWT::ClampedBufferCompare);
			#ifdef DBG
				FMeasure Measure;
				Measure.P1 = P1;
				Measure.P2 = P2;
			#endif

			BYTE* B1 = CompressBuffer + P1;
			BYTE* B2 = CompressBuffer + P2;

			// fastest
			int ret = memcmp(B1, B2, CompressLength - Max(P1, P2));
			return ret == 0 ? P1 < P2 : ret < 0;

			unguardSlow;
		}
	#elif (USE_SORT == USE_qsort)
		static INT ClampedBufferCompare( const INT* P1, const INT* P2 )
		{
			guardSlow(FCodecBWT::ClampedBufferCompare);
			#ifdef DBG
				FMeasure Measure;
				Measure.P1 = *P1;
				Measure.P2 = *P2;
			#endif

			BYTE* B1 = CompressBuffer + *P1;
			BYTE* B2 = CompressBuffer + *P2;

			// fastest
			INT ret = memcmp(B1, B2, CompressLength - Max(*P1,*P2));
			return ret == 0 ? *P1 - *P2 : ret;

			// average
			INT ret2 = appMemcmp(B1, B2, CompressLength - Max(*P1,*P2));
			return ret2 == 0 ? *P1 - *P2 : ret2;

			// average
			for( INT Count=CompressLength-Max(*P1,*P2); Count>0; Count--,B1++,B2++ ) {
				INT B = *B1 - *B2;
				if (B == 0) continue;
				return B;
			}
			return *P1 - *P2; 

			// slow
			for( INT Count=CompressLength-Max(*P1,*P2); Count>0; Count--,B1++,B2++ ) {
				BYTE _B1 = *B1;
				BYTE _B2 = *B2;
				if (_B1 == _B2) continue;
				return _B1 < _B2 ? -1 : 1;
			}
			return *P1 - *P2; 

			// slowest, original
			for( INT Count=CompressLength-Max(*P1,*P2); Count>0; Count--,B1++,B2++ ) {
				if( *B1 < *B2 )
					return -1;
				else if( *B1 > *B2 )
					return 1;
			}
			return *P1 - *P2;

			unguardSlow;
		}
	#endif
public:
	#ifdef DBG
		static FLOAT TotalTime;
		static FTime StartTime;
		static INT Count;
	#endif

	UBOOL Encode( FArchive& In, FArchive& Out )
	{
		guard(FCodecBWT::Encode);

		#ifdef DBG
			TotalTime = 0.f;
			Count = 0;
		#endif

		#ifdef SHOW_PROGRESS			
			Warn.LocalPrint(TEXT("0%"));
			FString Progress;
		#endif

		TArray<BYTE> CompressBufferArray(MAX_BUFFER_SIZE);
		CompressBuffer = &CompressBufferArray(0);
		#if (USE_SORT == USE_bwtsort)
			KeyPrefix* CompressPos;
		#else
			TArray<INT>  CompressPosition   (MAX_BUFFER_SIZE+1);
			INT* CompressPos = &CompressPosition(0);
		#endif		
		INT i, First=0, Last=0;
		while( !In.AtEnd() )
		{
			#ifdef DBG
				TotalTime = 0.f;
				Count = 0;
			#endif
			CompressLength = Min<INT>( In.TotalSize()-In.Tell(), MAX_BUFFER_SIZE );
			CompressLength = Min<INT>( CompressLength, RealBufferSize ); // reduce buffer for avoid slow down
			In.Serialize( CompressBuffer, CompressLength );
			#if (USE_SORT != USE_bwtsort)
				for( i=0; i<CompressLength+1; i++ ) CompressPos[i] = i;
			#endif
			#if (USE_SORT == USE_stable_sort)
				std::stable_sort(&CompressPos[0], &CompressPos[CompressLength], ClampedBufferCompare2);
			#elif (USE_SORT == USE_sort)
				std::sort(&CompressPos[0], &CompressPos[CompressLength], ClampedBufferCompare2);
			#elif (USE_SORT == USE_qsort)
				appQsort( &CompressPos[0], CompressLength+1, sizeof(INT), (QSORT_COMPARE)ClampedBufferCompare );
			#elif (USE_SORT == USE_bwtsort)
				CompressPos = bwtsort(CompressBuffer, CompressLength);
				CompressPos[CompressLength].offset = CompressLength;
			#endif
			for( i=0; i<CompressLength+1; i++ ) {
				INT pos = CompressPos[i];
				if( pos==1 )
					First = i;
				else if( pos==0 )
					Last = i;
			}
			Out << CompressLength << First << Last;
			for( i=0; i<CompressLength+1; i++ ) {
				INT pos = CompressPos[i];
				Out << CompressBuffer[pos?pos-1:0];
			}
			#if (USE_SORT == USE_bwtsort)
				free(CompressPos);
			#endif

			#ifdef SHOW_PROGRESS				
				Warn.LocalPrint(*FString::Printf(
					#ifdef DBG
						TEXT("\n")
					#else
						TEXT("\r")
					#endif
					TEXT("%.3f%%"), 100.f*In.Tell()/In.TotalSize()));
				#ifdef DBG
					Warn.LocalPrint(*FString::Printf(TEXT("\t %i\t %f"), Count, TotalTime));
				#endif
			#endif
		}
		#ifdef SHOW_PROGRESS
			Warn.LocalPrint(TEXT("\r"));
		#endif

		#ifdef DBG
			GWarn->Logf(TEXT("DBG: %f secs on %i times"), TotalTime, Count);
		#endif

		return 0;
		unguard;
	}
	UBOOL Decode( FArchive& In, FArchive& Out )
	{
		guard(FCodecBWT::Decode);
		TArray<BYTE> DecompressBuffer(MAX_BUFFER_SIZE+1);
		TArray<INT>  Temp(MAX_BUFFER_SIZE+1);
		INT DecompressLength, DecompressCount[256+1], RunningTotal[256+1], i, j;
		while( !In.AtEnd() )
		{
			INT First, Last;
			In << DecompressLength << First << Last;
			check(DecompressLength<=MAX_BUFFER_SIZE+1);
			check(DecompressLength<=In.TotalSize()-In.Tell());
			In.Serialize( &DecompressBuffer(0), ++DecompressLength );
			for( i=0; i<257; i++ )
				DecompressCount[ i ]=0;
			for( i=0; i<DecompressLength; i++ )
				DecompressCount[ i!=Last ? DecompressBuffer(i) : 256 ]++;
			INT Sum = 0;
			for( i=0; i<257; i++ )
			{
				RunningTotal[i] = Sum;
				Sum += DecompressCount[i];
				DecompressCount[i] = 0;
			}
			for( i=0; i<DecompressLength; i++ )
			{
				INT Index = i!=Last ? DecompressBuffer(i) : 256;
				Temp(RunningTotal[Index] + DecompressCount[Index]++) = i;
			}
			for( i=First,j=0 ; j<DecompressLength-1; i=Temp(i),j++ )
				Out << DecompressBuffer(i);
		}
		return 1;
		unguard;
	}
};
BYTE* FCodecBWT_fast::CompressBuffer;
INT   FCodecBWT_fast::CompressLength;

#ifdef DBG
	FLOAT FCodecBWT_fast::TotalTime;
	FTime FCodecBWT_fast::StartTime;
	INT   FCodecBWT_fast::Count;
	FMeasure::FMeasure() {
		FCodecBWT_fast::StartTime = appSeconds();
	};
	FMeasure::~FMeasure() {		
		FCodecBWT_fast::StartTime = appSeconds() - FCodecBWT_fast::StartTime; 
		FLOAT time = FCodecBWT_fast::StartTime.GetFloat();
		FCodecBWT_fast::TotalTime += time;
		FCodecBWT_fast::Count++;
		//if (time > 0.01) Warn.LocalPrint(*FString::Printf(TEXT("%f\t%i\t%i\n"), time, P1, P2));
	};
#endif

/*-----------------------------------------------------------------------------
	RLE compressor.
-----------------------------------------------------------------------------*/

class FCodecRLE_fast : public FCodec
{
private:
	enum {RLE_LEAD=5};
	UBOOL EncodeEmitRun( FArchive& Out, BYTE Char, BYTE Count )
	{
		for( INT Down=Min<INT>(Count,RLE_LEAD); Down>0; Down-- )
			Out << Char;
		if( Count>=RLE_LEAD )
			Out << Count;
		return 1;
	}
public:
	UBOOL Encode( FArchive& In, FArchive& Out )
	{
		guard(FCodecRLE::Encode);
		BYTE PrevChar=0, PrevCount=0, B;
		while( !In.AtEnd() )
		{
			In << B;
			if( B!=PrevChar || PrevCount==255 )
			{
				EncodeEmitRun( Out, PrevChar, PrevCount );
				PrevChar  = B;
				PrevCount = 0;
			}
			PrevCount++;
		}
		EncodeEmitRun( Out, PrevChar, PrevCount );
		return 0;
		unguard;
	}
	UBOOL Decode( FArchive& In, FArchive& Out )
	{
		guard(FCodecRLE::Decode);
		INT Count=0;
		BYTE PrevChar=0, B, C;
		while( !In.AtEnd() )
		{
			In << B;
			Out << B;
			if( B!=PrevChar )
			{
				PrevChar = B;
				Count    = 1;
			}
			else if( ++Count==RLE_LEAD )
			{
				In << C;
				check(C>=2);
				while( C-->RLE_LEAD )
					Out << B;
				Count = 0;
			}
		}
		return 1;
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	Huffman codec.
-----------------------------------------------------------------------------*/

class FCodecHuffman_fast : public FCodec
{
private:
	struct FHuffman
	{
		INT Ch, Count;
		TArray<FHuffman*> Child;
		TArray<BYTE> Bits;
		FHuffman( INT InCh )
		: Ch(InCh), Count(0)
		{
		}
		~FHuffman()
		{
			for( INT i=0; i<Child.Num(); i++ )
				delete Child( i );
		}
		void PrependBit( BYTE B )
		{
			Bits.Insert( 0 );
			Bits(0) = B;
			for( INT i=0; i<Child.Num(); i++ )
				Child(i)->PrependBit( B );
		}
		void WriteTable( FBitWriter& Writer )
		{
			Writer.WriteBit( Child.Num()!=0 );
			if( Child.Num() )
				for( INT i=0; i<Child.Num(); i++ )
					Child(i)->WriteTable( Writer );
			else
			{
				BYTE B = Ch;
				Writer << B;
			}
		}
		void ReadTable( FBitReader& Reader )
		{
			if( Reader.ReadBit() )
			{
				Child.Add( 2 );
				for( INT i=0; i<Child.Num(); i++ )
				{
					Child( i ) = new FHuffman( -1 );
					Child( i )->ReadTable( Reader );
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
		INT SavedPos = In.Tell();
		INT Total=0, i;

		// Compute character frequencies.
		TArray<FHuffman*> Huff(256);
		for( i=0; i<256; i++ )
			Huff(i) = new FHuffman(i);
		TArray<FHuffman*> Index = Huff;
		while( !In.AtEnd() )
			Huff(Arctor<BYTE>(In))->Count++, Total++;
		In.Seek( SavedPos );
		Out << Total;

		// Build compression table.
		while( Huff.Num()>1 && Huff.Last()->Count==0 )
			delete Huff.Pop();
		INT BitCount = Huff.Num()*(8+1);
		while( Huff.Num()>1 )
		{
			FHuffman* Node  = new FHuffman( -1 );
			Node->Child.Add( 2 );
			for( i=0; i<Node->Child.Num(); i++ )
			{
				Node->Child(i) = Huff.Pop();
				Node->Child(i)->PrependBit(i);
				Node->Count += Node->Child(i)->Count;
			}
			for( i=0; i<Huff.Num(); i++ )
				if( Huff(i)->Count < Node->Count )
					break;
			Huff.Insert( i );
			Huff( i ) = Node;
			BitCount++;
		}
		FHuffman* Root = Huff.Pop();

		// Calc stats.
		while( !In.AtEnd() )
			BitCount += Index(Arctor<BYTE>(In))->Bits.Num();
		In.Seek( SavedPos );

		// Save table and bitstream.
		FBitWriter Writer( BitCount );
		Root->WriteTable( Writer );
		while( !In.AtEnd() )
		{
			FHuffman* P = Index(Arctor<BYTE>(In));
			for( INT i=0; i<P->Bits.Num(); i++ )
				Writer.WriteBit( P->Bits(i) );
		}
		check(!Writer.IsError());
		check(Writer.GetNumBits()==BitCount);
		Out.Serialize( Writer.GetData(), Writer.GetNumBytes() );

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
		while( Total-- > 0 )
		{
			check(!Reader.AtEnd());
			FHuffman* Node = &Root;
			while( Node->Ch==-1 )
				Node = Node->Child( Reader.ReadBit() );
			BYTE B = Node->Ch;
			Out << B;
		}
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
		BYTE List[256], B, C;
		INT i;
		for( i=0; i<256; i++ )
			List[i] = i;
		while( !In.AtEnd() )
		{
			In << B;
			for( i=0; i<256; i++ )
				if( List[i]==B )
					break;
			check(i<256);
			C = i;
			Out << C;
			INT NewPos=0;
			for( i; i>NewPos; i-- )
				List[i]=List[i-1];
			List[NewPos] = B;
		}
		return 0;
		unguard;
	}
	UBOOL Decode( FArchive& In, FArchive& Out )
	{
		guard(FCodecMTF::Decode);
		BYTE List[256], B, C;
		INT i;
		for( i=0; i<256; i++ )
			List[i] = i;
		while( !In.AtEnd() )
		{
			In << B;
			C = List[B];
			Out << C;
			INT NewPos=0;
			for( i=B; i>NewPos; i-- )
				List[i]=List[i-1];
			List[NewPos] = C;
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
