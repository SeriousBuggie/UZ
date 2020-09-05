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

//#define USE_SORT // use sort instead of qsort

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

INT RealBufferSize = 0x2000;

class FCodecBWT_fast : public FCodec
{
private:
	enum {MAX_BUFFER_SIZE=0x40000}; /* Hand tuning suggests this is an ideal size */
	static BYTE* CompressBuffer;
	static INT CompressLength;
	#ifdef USE_SORT
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
	#else
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
		TArray<INT>  CompressPosition   (MAX_BUFFER_SIZE+1);
		CompressBuffer = &CompressBufferArray(0);
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
			for( i=0; i<CompressLength+1; i++ )
				CompressPosition(i) = i;
			#ifdef USE_SORT
				std::sort(&CompressPosition(0), &CompressPosition(CompressLength), ClampedBufferCompare2);
			#else
				appQsort( &CompressPosition(0), CompressLength+1, sizeof(INT), (QSORT_COMPARE)ClampedBufferCompare );
			#endif
			for( i=0; i<CompressLength+1; i++ ) {
				INT pos = CompressPosition(i);
				if( pos==1 )
					First = i;
				else if( pos==0 )
					Last = i;
			}
			Out << CompressLength << First << Last;
			for( i=0; i<CompressLength+1; i++ ) {
				INT pos = CompressPosition(i);
				Out << CompressBuffer[pos?pos-1:0];
			}
			//GWarn->Logf(TEXT("Compression table"));
			//for( i=0; i<CompressLength+1; i++ )
			//	GWarn->Logf(TEXT("    %03i: %s"),CompressPosition(i)?CompressBuffer[CompressPosition(i)-1]:-1,appFromAnsi((ANSICHAR*)CompressBuffer+CompressPosition(i)));

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
				TEXT("\t%s compress files_for_compress [newformat] [update] [buffer=N]\n")
				TEXT("\t%s decompress files_for_decompress\n\n")
				TEXT("\tnewformat\tApplies run-length encoding to the compressed files. This increases the compression rate.\n")
				TEXT("\tupdate\t\tOnly compress if the uz file does not exist, or if it is older than the corresponding source file.\n")
				TEXT("\tbuffer=N\tLimit BWT buffer to specified size. N is number, which can be in decimal or hexadecimal form (with prefix 0x). Bigger buffer - better but slower compression. Default limit: 0x%X"),
				*app, 
				*app,
				RealBufferSize);
		} else {
			int last = argc - 1;
			FString CFile;
			FString UFile;
			FCodecFull Codec[2];

			Codec[0].AddCodec(new FCodecRLE);
			Codec[0].AddCodec(new FCodecBWT_fast);
			Codec[0].AddCodec(new FCodecMTF);
			Codec[0].AddCodec(new FCodecHuffman);

			Codec[1].AddCodec(new FCodecRLE);
			Codec[1].AddCodec(new FCodecBWT_fast);
			Codec[1].AddCodec(new FCodecMTF);
			Codec[1].AddCodec(new FCodecRLE);
			Codec[1].AddCodec(new FCodecHuffman);

			if (Token == TEXT("COMPRESS")) {
				bool newformat = false;
				bool update = false;
				bool extended = false;
				while (true) {
					Token = appFromAnsi(argv[last]);
					if (Token == TEXT("UPDATE")) {
						update = true;
					} else if (Token == TEXT("NEWFORMAT")) {
						newformat = true;
					} else if (Token.Left(7) == TEXT("BUFFER=")) {
						TCHAR** End = NULL;
						INT Size = wcstol(*Token + 7, End, 0);
						if (Size >= 10) {
							Warn.Logf(TEXT("BWT buffer size limited to %i (0x%X)"), Size, Size);
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
					if (extended) {
						Warn.Logf(TEXT("Compressed %s -> %s (%.3f%%)"), *UFile, *CFile, 100.f*GFileManager->FileSize(*CFile)/USize);
					} else {
						Warn.Logf(TEXT("Compressed %s -> %s (%i%%)"), *UFile, *CFile, (INT)(100.f*GFileManager->FileSize(*CFile)/USize + 0.5f));
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
