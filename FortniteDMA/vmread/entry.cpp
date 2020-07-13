#include "hlapi/hlapi.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <random>
#include <chrono>
#include <iostream>
#include <cfloat>
#include <thread>

#define DefaultZoom 0.5625f

typedef uint32_t DWORD;
typedef uint64_t QWORD;

FILE* dfile;
static WinProcess* TargetProcess{ nullptr };


bool CompareData(const char* Data, const char* Signature, const char* Mask)
{
	for (; *Mask; ++Mask, ++Data, ++Signature)
	{
		if (*Mask == 'x' && *Data != *Signature)
			return false;
	}
	return (*Mask == 0);
}

QWORD PatternFinder(QWORD Start, QWORD Size, const char* Signature, const char* Mask)
{
	auto Buffer = static_cast<char*>(malloc(Size));
	TargetProcess->Read(Start, Buffer, Size);

	for (QWORD i = 0; i < Size; i++)
	{
		if (CompareData(Buffer + i, Signature, Mask))
		{
			free(Buffer);
			return Start + i;
		}
	}

	free(Buffer);
	return 0;
}

QWORD Absolute(QWORD RIP, DWORD InstructionLength)
{
	return RIP + InstructionLength + TargetProcess->Read<DWORD>(RIP + InstructionLength - 4);
}


class Fortnite
{
public:

	const char* ImageName = "FortniteClient-Win64-Shipping.exe";
	QWORD ModuleBase{ 0x00 };
	QWORD ModuleSize{ 0x00 };
	QWORD UWorld{ 0x00 };
	QWORD UGameInstance{ 0x00 };
	QWORD ULocalPlayerArray{ 0x38 };
	QWORD ULocalPlayer{ 0x00 };
	QWORD GetViewPoint{ 0x00 };
	QWORD FOVOffsetAddress{ 0x00 };
	QWORD ZoomAddress{ 0x00 };
	QWORD FOVJmp{ 0x00 };
	DWORD FOVOffset{ 0x1B8 };
	DWORD LocalPlayerFOVOffset{ 0x00 };
    float CurZoom{ 0.0f };
    float CurFOV{ 0.0f };

	QWORD GetUWorld()
	{
		UWorld = TargetProcess->Read<QWORD>(AbsoluteScan(
			"\x48\x8B\x1D\x00\x00\x00\x00\x48\x85\xDB\x74\x3B\x41", 
			"xxx????xxxxxx", 7)
		); return UWorld;
	}
	
	QWORD GetGameInstance()
	{
		UGameInstance = TargetProcess->Read<QWORD>(UWorld + GetGameInstanceOffset());
		return UGameInstance;
	}

	QWORD GetPlayerArray()
	{
		ULocalPlayerArray = TargetProcess->Read<QWORD>(UGameInstance + ULocalPlayerArray);
		return ULocalPlayerArray;
	}

	QWORD GetLocalPlayer()
	{
		ULocalPlayer = TargetProcess->Read<QWORD>(ULocalPlayerArray);
        return ULocalPlayer;
	}

	DWORD GetGameInstanceOffset() 
	{
		QWORD TempGameInstance = 0;
		QWORD TempPlayerArray = 0;
		QWORD TempLocalPlayer = 0;
		float LocalPlayerFOV = 0.0f;

		LocalPlayerFOVOffset = TargetProcess->Read<DWORD>(Scan(
			"\xF3\x0F\x10\x47\x18\x0F\x2E\x83\x00\x00\x00\x00", 
			"xxxxxxxx??xx" 
		) + 8);

		for (DWORD Offset = 0x00; Offset < 0x420; Offset += 0x04)
		{
			TempGameInstance = TargetProcess->Read<QWORD>(UWorld + Offset);
			TempPlayerArray = TargetProcess->Read<QWORD>(TempGameInstance + 0x38);
			TempLocalPlayer = TargetProcess->Read<QWORD>(TempPlayerArray);
			LocalPlayerFOV = TargetProcess->Read<float>(TempLocalPlayer + LocalPlayerFOVOffset);

			if (LocalPlayerFOV == 80.0f || LocalPlayerFOV == 90.0f )
				return Offset;
		}

		return 0x190;
	}

	void SetZoom(float Zoom) const
	{
		TargetProcess->Write<float>(ZoomAddress, Zoom);
	}

	void SetFOV(float FOV) const
	{
		FOV = (((180.0f - 50.534008f) / 100.0f) * (FOV - 80.0f)) + 50.534008f;
		TargetProcess->Write<float>(ULocalPlayer + FOVOffset, FOV);
	}
    
    float GetFOV()
    {   
        CurFOV = TargetProcess->Read<float>( ULocalPlayer + LocalPlayerFOVOffset);
        return CurFOV;
    }    
    
    float GetZoom()
    {
        CurZoom = TargetProcess->Read<float>(ZoomAddress);
        return CurZoom;
    }
    
    void UpdateZoom() const
    {
        TargetProcess->Write<float>(ULocalPlayer + LocalPlayerFOVOffset, 1.0f);
    }

	WinModule GetModuleInfo(const char* ModuleName) const
	{
		return TargetProcess->modules.GetModuleInfo(ModuleName)->info;
	}

	void GetProcessInfo()
	{
		WinModule MainModule = GetModuleInfo(ImageName);
		ModuleBase = MainModule.baseAddress;
		ModuleSize = MainModule.sizeOfModule;
	}

	QWORD Scan(const char* Signature, const char* Mask)
	{
		return PatternFinder(ModuleBase, ModuleSize, Signature, Mask);
	}

	QWORD Scan(QWORD Start, QWORD Size, const char* Signature, const char* Mask)
	{
		return PatternFinder(Start, Size, Signature, Mask);
	}

	QWORD AbsoluteScan(const char* Signature, const char* Mask, DWORD InstructionLength)
	{
		return Absolute(Scan(Signature, Mask), InstructionLength);
	}

	QWORD AbsoluteScan(QWORD Start, QWORD Size, const char* Signature, const char* Mask, DWORD InstructionLength)
	{
		return Absolute(Scan(Start, Size, Signature, Mask), InstructionLength);
	}

    void Init()
	{
		GetProcessInfo();
        GetUWorld();
        GetGameInstance();
        GetPlayerArray();
        GetLocalPlayer();

		GetViewPoint = Scan(
			"\x74\x00\x44\x8B\xC6\xC6\x83\x00\x00\x00\x00\x01\x48\x8B\xD7\x48\x8B\xCB\xE8", 
			"x?xxxxx??xxxxxxxxxx" 
		);

        ZoomAddress = AbsoluteScan(GetViewPoint, 0x1024, 
			"\xF3\x0F\x59\x05\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xF3\x0F\x59\x05\x00\x00\x00\x00\xF3\x0F\x11", 
			"xxxx????x????xxxx????xxx", 8
		);

		FOVOffsetAddress = Scan(GetViewPoint, 0x1024, 
			"\xF3\x0F\x10\x83\x00\x00\x00\x00\xF3\x0F\x11\x47\x18", 
			"xxxx??xxxxxxx" 
		) + 4;

        FOVJmp = Scan( GetViewPoint, 0x1024, 
			"\xEB\x08", 
			"xx" 
		);

		uint8_t JmpRIP[2]{ 0xEB, 0x00 };
		TargetProcess->Write <DWORD>(FOVOffsetAddress, FOVOffset);
		TargetProcess->Write(FOVJmp, JmpRIP, sizeof(JmpRIP));
	}
};


__attribute__((constructor))
static void init()
{
	FILE* out = stdout;
	pid_t pid;

#if (LMODE() == MODE_EXTERNAL())
	FILE* pipe = popen("pidof qemu-system-x86_64", "r");
	fscanf(pipe, "%d", &pid);
	pclose(pipe);

#else

	out = fopen("/tmp/testr.txt", "w");
	pid = getpid();

#endif

	dfile = out;

	try
	{
		WinContext ctx(pid);
		ctx.processList.Refresh();

		for (auto& CurProcess : ctx.processList)
		{
			if (!strcasecmp("FortniteClient-Win64-Shipping.exe", CurProcess.proc.name))
			{
				fprintf(out, "\nFound process %lx:\t%s\n", CurProcess.proc.pid, CurProcess.proc.name);
				TargetProcess = &CurProcess;

				auto FN{ std::make_unique<Fortnite>() };
				FN->Init();
				FN->SetFOV(105.0f);

				fprintf(out, "\nBase Address:\t%p",  (void*)FN->ModuleBase);
				fprintf(out, "\nModule Size:\t%p",	 (void*)FN->ModuleSize);
				fprintf(out, "\nUWorld:\t\t%p",		 (void*)FN->UWorld);
				fprintf(out, "\nUGameInstance:\t%p", (void*)FN->UGameInstance);
				fprintf(out, "\nPlayerArray:\t%p",   (void*)FN->ULocalPlayerArray);
				fprintf(out, "\nULocalPlayer:\t%p",  (void*)FN->ULocalPlayer);
				fprintf(out, "\nGetViewPoint:\t%p",  (void*)FN->GetViewPoint);
                fprintf(out, "\nZoom Address:\t%p",  (void*)FN->ZoomAddress);
                fprintf(out, "\n" );
			}
		}
	}

	catch (VMException& e)
	{
		fprintf(out, "Initialization error: %d\n", e.value);
	}
}

int main()
{
	return 0;
}
