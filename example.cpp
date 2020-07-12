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
#include <math.h>
#include "offsets.h"
#include "vector.h"
#include <thread>
#include <X11/Xlib.h>
#include "X11/keysym.h"

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
FILE* dfile;

static WinProcess* target_process{};

bool compare_data( const char* Data, const char* Signature, const char* Mask )
{
	for ( ; *Mask; ++Mask, ++Data, ++Signature )
	{
		if ( *Mask == 'x' && *Data != *Signature )
			return false;
	}
	return ( *Mask == 0 );
}

uint64_t pattern_finder( uint64_t start_address, uint64_t search_size, const char* signature, const char* mask )
{
	auto buffer = static_cast < char* > ( malloc( search_size ) );

	target_process->Read( start_address, buffer, search_size );

	for ( uint64_t i = 0; i < search_size; i++ )
	{
		if ( compare_data( buffer + i, signature, mask ) )
		{
			free( buffer );
			return start_address + i;
		}
	}

	free( buffer );
	return 0;
}

uint64_t absolute_address( uint64_t rip, uint32_t instruction_length )
{
	uint32_t relative_offset = target_process->Read< uint32_t > ( rip + instruction_length - 4 );
	return rip + instruction_length + relative_offset;
}

class Fortnite
{

public:

	const char* image_name = "FortniteClient-Win64-Shipping.exe";

	uint64_t base_address{};
	uint64_t module_size{};
	uint64_t uworld{};
	uint64_t ugame_instance{};
	uint64_t ulocal_player_array{ 0x38 };
	uint64_t ulocal_player{};
	uint64_t get_view_point{};
	uint64_t fov_offset_address{};
	uint64_t zoom_address{};
	uint64_t fov_jmp{};
	uint32_t fov_offset{ 0x1B8 };
	uint32_t local_player_fov_offset{};


	uint64_t absolute( uint64_t rip, uint32_t instruction_length )
	{
		return absolute_address( rip, instruction_length );
	}

	uint64_t scan( const char* signature, const char* mask )
	{
		return pattern_finder( base_address, module_size, signature, mask );
	}

	uint64_t scan( uint64_t start_address, uint64_t search_size, const char* signature, const char* mask )
	{
		return pattern_finder( start_address, search_size, signature, mask );
	}

    uint32_t get_game_instance_offset()
	{
		uint64_t temp_game_instance = 0;
		uint64_t temp_player_array = 0;
		uint64_t temp_local_player = 0;
		float local_player_fov = 0.0f;

		local_player_fov_offset = target_process->Read < uint32_t > ( scan( 
			"\xF3\x0F\x10\x47\x18\x0F\x2E\x83\x00\x00\x00\x00", 
			"xxxxxxxx??xx" 
		) + 8 );

		for ( uint32_t offset = 0x00; offset < 0x420; offset += 0x04 )
		{
			temp_game_instance = target_process->Read < uint64_t > ( uworld + offset );
			temp_player_array = target_process->Read < uint64_t > ( temp_game_instance + 0x38 );
			temp_local_player = target_process->Read < uint64_t > ( temp_player_array );
			local_player_fov = target_process->Read < float > ( temp_local_player + local_player_fov_offset );

			if ( local_player_fov == 80.0f || local_player_fov == 90.0f )
				return offset;
		}

		return 0x190;
	}

	uint64_t get_uworld()
	{
		uworld = target_process->Read < uint64_t > ( absolute( scan( "\x48\x8B\x1D\x00\x00\x00\x00\x48\x85\xDB\x74\x3B\x41", "xxx????xxxxxx" ), 7 ) );
		return uworld;
	}
	
	uint64_t get_game_instance()
	{
		ugame_instance = target_process->Read < uint64_t > ( uworld + 0x190 );
		return ugame_instance;
	}

	uint64_t get_player_array()
	{
		ulocal_player_array = target_process->Read < uint64_t > ( ugame_instance + ulocal_player_array );
		return ulocal_player_array;
	}

	uint64_t get_local_player()
	{
		ulocal_player = target_process->Read < uint64_t > ( ulocal_player_array );
        return ulocal_player;
	}

	void set_fov( float fov ) const
	{
		fov = ( ( ( 180.0f - 50.534008f ) / 100.0f ) * ( fov - 80.0f ) ) + 50.534008f;
		target_process->Write < float > ( ulocal_player + fov_offset, fov );
	}

	void set_zoom( float zoom ) const
	{
		target_process->Write < float > ( zoom_address, zoom );
	}

	void get_process_info()
	{
		base_address = target_process->GetPeb().ImageBaseAddress;
		module_size = target_process->GetModuleInfo( image_name )->info.sizeOfModule;
	}

    void init( float fov )
	{
		get_process_info();
        get_uworld();
        get_game_instance();
        get_player_array();
        get_local_player();

		get_view_point = scan(
			"\x74\x00\x44\x8B\xC6\xC6\x83\x00\x00\x00\x00\x01\x48\x8B\xD7\x48\x8B\xCB\xE8", 
			"x?xxxxx??xxxxxxxxxx" 
		);

		fov_offset_address = scan( get_view_point, 0x1024, 
			"\xF3\x0F\x10\x83\x00\x00\x00\x00\xF3\x0F\x11\x47\x18", 
			"xxxx??xxxxxxx" 
		) + 4;

		zoom_address = absolute( scan( get_view_point, 0x1024, 
			"\xF3\x0F\x59\x05\x00\x00\x00\x00\xE8\x00\x00\x00\x00\xF3\x0F\x59\x05\x00\x00\x00\x00\xF3\x0F\x11", 
			"xxxx????x????xxxx????xxx" 
		), 8 );

		fov_jmp = scan( get_view_point, 0x1024, 
			"\xEB\x08", 
			"xx" 
		);

		uint8_t jmp_rip [ 2 ] { 0xEB, 0x00 };

		target_process->Write < uint32_t > ( fov_offset_address, fov_offset );
		target_process->Write( fov_jmp, jmp_rip, sizeof( jmp_rip ) );

		if ( fov )
			set_fov( fov );
	}
};

//__attribute__((constructor))
static void init( )
{
	FILE* out = stdout;
	/*pid_*/t pid;
#if (LMODE() == MODE_EXTERNAL())
	FILE* pipe = popen("pidof qemu-system-x86_64", "r");
	fscanf(pipe, "%d", &pid);
	pclose(pipe);
#else
	out = fopen("/tmp/testr.txt", "w");
	pid = getpid();
#endif
	fprintf(out, "Using Mode: %s\n", TOSTRING(LMODE));

	dfile = out;

	try
	{
		WinContext ctx(pid);
		ctx.processList.Refresh();

		for (auto& cur_process : ctx.processList)
		{
			if (!strcasecmp("FortniteClient-Win64-Shipping.exe", cur_process.proc.name))
			{
				fprintf(out, "\nFound process %lx:\t%s\n", cur_process.proc.pid, cur_process.proc.name);
				target_process = &cur_process;

				auto fn = std::make_unique < Fortnite > ( );
				fn->init( 105.0f );
				
				fprintf( out, "\nBase Address:\t%lx", fn->base_address );
				fprintf( out, "\nModule Size:\t%lx", fn->module_size );
				fprintf( out, "\nUWorld:\t%lx", fn->uworld );
				fprintf( out, "\nUGameInstance:\t%lx", fn->ugame_instance );
				fprintf( out, "\nPlayerArray:\t%lx", fn->ulocal_player_array );
				fprintf( out, "\nULocalPlayer:\t%lx", fn->ulocal_player );
				fprintf( out, "\nGetViewPoint:\t%lx", fn->get_view_point );
                fprintf( out, "\n" );
			}
		}


	}

	catch (VMException& e)
	{
		fprintf(out, "Initialization error: %d\n", e.value);
	}

	fclose(out);
}

int main()
{
	return 0;
}