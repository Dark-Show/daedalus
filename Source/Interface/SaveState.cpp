/*
Copyright (C) 2001 Lkb

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "Base/Daedalus.h"
#include "Interface/SaveState.h"

#include <stdio.h>

#include "Base/MathUtil.h"
#include "Core/CPU.h"
#include "Core/Dynamo.h"
#include "Core/Memory.h"
#include "Core/R4300.h"
#include "Core/ROM.h"
#include "Debug/Console.h"
#include "Interface/RomDB.h"
#include "Main/SystemInit.h"
#include "OSHLE/OSHLE.h"
#include "RomFile/RomFile.h"
#include "System/CompressedStream.h"
#include "System/Mutex.h"
#include "Ultra/ultra_R4300.h"

//
//	SaveState code written initially by Lkb. Seems to be based about Project 64's
//	savestate format, which is partially documented here: http://www.hcs64.com/usf/usf.txt
//

const u32 SAVESTATE_PROJECT64_MAGIC_NUMBER = 0x23D8A6C8;

static Mutex gSaveStateMutex;
static std::string gSaveStateFilename = "";

enum ESaveStateOperation
{
	SSO_NONE,
	SSO_SAVE,
	SSO_LOAD,
};

static ESaveStateOperation gSaveStateOperation = SSO_NONE;

class SaveStateOutput
{
public:
	SaveStateOutput( const std::string& filename )
		: mStream( filename )
	{
	}

	template<typename T>
	inline SaveStateOutput& operator << (const T& data)
	{
		write(&data, sizeof(T));
		return *this;
	}

	inline void write_memory_buffer(int buffernum, u32 size = 0)
	{
		write(gMemBuffers[buffernum], size ? Min(gMemBufferSizes[buffernum], size) : gMemBufferSizes[buffernum]);
		if(size > gMemBufferSizes[buffernum])
			skip(size - gMemBufferSizes[buffernum]);
	}

	// I had to add this as the PIF ram now lies in the same chunk as the PIF rom.
	inline void write_memory_buffer_offset(int buffernum, u32 offset, u32 size )
	{
		write((u8*)gMemBuffers[buffernum] + offset, size ? Min(gMemBufferSizes[buffernum], size) : gMemBufferSizes[buffernum]);
		if(size > gMemBufferSizes[buffernum])
			skip(size - gMemBufferSizes[buffernum]);
	}

	bool IsValid() const
	{
		return mStream.IsOpen();
	}

	void skip( size_t size )
	{
		const size_t	MAX_ZEROES = 512;
		char			zeroes[ MAX_ZEROES ];
		if( mStream.IsOpen() )
		{
			memset( zeroes, 0, sizeof( zeroes ) );

			size_t	remaining( size );
			while( remaining > 0 )
			{
				u32		max_to_write( Min( remaining, MAX_ZEROES ) );

				mStream.WriteData( zeroes, max_to_write );

				remaining -= max_to_write;
			}
		}
	}

	size_t write( const void * data, size_t size )
	{
		if( mStream.WriteData( data, size ))
			return size;

		return 0;
	}

private:
	COutStream		mStream;
};

class SaveStateInput
{
public:
	explicit SaveStateInput( const std::string& filename )
		: mStream( filename )
	{}

	inline bool IsValid() const
	{
		return mStream.IsOpen();
	}

	template<typename T>
	inline SaveStateInput& operator >> (T& data)
	{
		if (read(&data, sizeof(data)) != sizeof(data))
		{
			memset(&data, 0, sizeof(data));
		}
		return *this;
	}

	inline void read_memory_buffer(int buffernum, u32 size = 0)
	{
		read(gMemBuffers[buffernum], size ? Min(gMemBufferSizes[buffernum], size) : gMemBufferSizes[buffernum]);
		if(size > gMemBufferSizes[buffernum])
			skip(size - gMemBufferSizes[buffernum]);
	}

	inline void read_memory_buffer_offset(int buffernum, u32 offset, u32 size )
	{
		read((u8*)gMemBuffers[buffernum] + offset, size ? Min(gMemBufferSizes[buffernum], size) : gMemBufferSizes[buffernum]);
		if(size > gMemBufferSizes[buffernum])
			skip(size - gMemBufferSizes[buffernum]);
	}

	inline void read_memory_buffer_write_value(int buffernum, int address)
	{
		for( u32 i = 0; i < gMemBufferSizes[buffernum]; i += 4 )
		{
			u32 value;
			*this >> value;
			Write32Bits(address + i, value);
		}
	}

	void skip( size_t size )
	{
		if( mStream.IsOpen() )
		{
			const size_t	BUFFER_SIZE = 512;
			u8				buffer[ BUFFER_SIZE ];

			size_t		remaining( size );
			while( remaining > 0 )
			{
				u32		max_to_read( Min( remaining, BUFFER_SIZE ) );

				mStream.ReadData( buffer, max_to_read );

				// Discard bytes..
				remaining -= max_to_read;
			}
		}
	}

	size_t read(void* data, size_t size)
	{
		if( mStream.ReadData( data, size ) )
			return size;

		return 0;
	}

private:
	CInStream			mStream;
};


static bool SaveStateToFile( const std::string& filename )
{
	SaveStateOutput stream( filename );

	if( !stream.IsValid() )
		return false;

	stream << SAVESTATE_PROJECT64_MAGIC_NUMBER;
	stream << gRamSize;
	ROMHeader rom_header;
	memcpy(&rom_header, &g_ROM.rh, 64);
	ROMFile::ByteSwap_3210(&rom_header, 64);
	stream << rom_header;
	stream << Max< u32 >(CPU_GetVideoInterruptEventCount(), 1);

	stream << gCPUState.CurrentPC;
	stream.write(gGPR, 256);
	u32 i;
	for(i = 0; i < 32; i++)
	{
		stream << gCPUState.FPU[i]._u32;
	}
	stream.skip(0x80); // used when FPU is in 64-bit mode
	for(i = 0; i < 32; i++)
	{
		stream << gCPUState.CPUControl[i]._u32;
	}
	for(i = 0; i < 32; i++)
	{
		stream << gCPUState.FPUControl[i]._u32;
	}
	stream << gCPUState.MultHi._u64;
	stream << gCPUState.MultLo._u64;

	stream.write_memory_buffer(MEM_RD_REG0, 0x28);
	stream.write_memory_buffer(MEM_SP_REG,  0x28);
	stream.write_memory_buffer(MEM_DPC_REG, 0x28);

	stream.write_memory_buffer(MEM_MI_REG);
	stream.write_memory_buffer(MEM_VI_REG);
	stream.write_memory_buffer(MEM_AI_REG);
	stream.write_memory_buffer(MEM_PI_REG);
	stream.write_memory_buffer(MEM_RI_REG);
	stream << Memory_SI_GetRegister( SI_DRAM_ADDR_REG );
	stream << Memory_SI_GetRegister( SI_PIF_ADDR_RD64B_REG );
	stream << Memory_SI_GetRegister( SI_PIF_ADDR_WR64B_REG );
	stream << Memory_SI_GetRegister( SI_STATUS_REG );
	for(i = 0; i < 32; i++)
	{
		// boolean that tells whether the entry is defined
		stream << (u32)(((g_TLBs[i].pfne & TLBLO_V) || (g_TLBs[i].pfno & TLBLO_V)) ? 1 : 0);
		stream << (u32)g_TLBs[i].pagemask;
		stream << (u32)g_TLBs[i].hi;
		stream << (u32)g_TLBs[i].pfne;
		stream << (u32)g_TLBs[i].pfno;
	}

	stream.write( gMemBuffers[MEM_PIF_RAM], 0x40);
	stream.write( gMemBuffers[MEM_RD_RAM], gRamSize);
	stream.write_memory_buffer(MEM_SP_MEM);
	return true;
}

// In revision >=715 we were byte swapping PIF RAM in a temp buffer, this broke compatibility with PJ64 saves
// Now that is fixed this been added for compatibility reasons for any ss created within those revs..
static void Swap_PIF()
{
	u8 * pPIFRam = (u8 *)gMemBuffers[MEM_PIF_RAM];

	if(pPIFRam[0] & 0xC0)
	{
		printf("No need to swap\n");
		return;
	}

	u8 temp[64];
	memcpy( temp, pPIFRam, 64 );

	for (u32 i = 0; i < 64; i++)
	{
		pPIFRam[i] = temp[ i ^ U8_TWIDDLE ];
	}
}

static bool LoadStateFromFile( const std::string& filename )
{
	SaveStateInput stream( filename );

	if( !stream.IsValid() )
	{
		return false;
	}

	u32 value;
	stream >> value;
	if(value != SAVESTATE_PROJECT64_MAGIC_NUMBER)
	{
		Console_Print("Wrong magic number - savestate could be damaged or not in Daedalus/Project64 format");
		return false;
	}
	stream >> gRamSize;
	ROMHeader rom_header;
	stream >> rom_header;
	ROMFile::ByteSwap_3210(&rom_header, 64);

	RomID	new_rom_id( rom_header.CRC1, rom_header.CRC2, rom_header.CountryID );

	if(g_ROM.mRomID != new_rom_id)
	{
		//ToDo: Give Option to switch Roms to one listed in SaveState if available.
		Console_Print("ROM name in savestate is different from the name of the currently loaded ROM: %x-%x-%02x, %x-%x-%02x\n",
			g_ROM.mRomID.CRC[0], g_ROM.mRomID.CRC[1], g_ROM.mRomID.CountryID,
			new_rom_id.CRC[0], new_rom_id.CRC[1], new_rom_id.CountryID);
		return false;
	}

	u32 count = 0;
	stream >> count;
	CPU_SetVideoInterruptEventCount( count );
	stream >> value;
	CPU_SetPC(value);
	stream.read(gGPR, 256);
	int i;
	for(i = 0; i < 32; i++)
	{
		stream >> value;
		gCPUState.FPU[i]._u32 = value;
	}
	stream.skip(0x80); // used when FPU is in 64-bit mode
	u32 g_dwNewCPR0[32];
	for(i = 0; i < 32; i++)
	{
		stream >> g_dwNewCPR0[i];
	}
	for(i = 0; i < 32; i++)
	{
		stream >> value;
		gCPUState.FPUControl[i]._u32 = value;
	}
	stream >> gCPUState.MultHi._u64;
	stream >> gCPUState.MultLo._u64;
	stream.read_memory_buffer(MEM_RD_REG0, 0x28); //, 0x84040000);
	stream.read_memory_buffer(MEM_SP_REG, 0x28); //, 0x84040000);

	u8* dpcRegData = new u8[gMemBufferSizes[MEM_DPC_REG]];
	stream.read(dpcRegData, gMemBufferSizes[MEM_DPC_REG]);
	memcpy(gMemBuffers[MEM_DPC_REG], dpcRegData, gMemBufferSizes[MEM_DPC_REG]);

	stream.skip(8); // PJ64 stores 10 MEM_DP_COMMAND_REGs

	u8* miRegData = new u8[gMemBufferSizes[MEM_MI_REG]];
	stream.read(miRegData, gMemBufferSizes[MEM_MI_REG]);
	memcpy(gMemBuffers[MEM_MI_REG], miRegData, gMemBufferSizes[MEM_MI_REG]);

	stream.read_memory_buffer_write_value(MEM_VI_REG, 0x84400000); // call WriteValue to update global and GFX plugin data
	stream.read_memory_buffer_write_value(MEM_AI_REG, 0x84500000); // call WriteValue to update audio plugin data

	// here to undo any modifications done by plugins
	memcpy(gMemBuffers[MEM_DPC_REG], dpcRegData, gMemBufferSizes[MEM_DPC_REG]);
	delete [] dpcRegData;
	memcpy(gMemBuffers[MEM_MI_REG], miRegData, gMemBufferSizes[MEM_MI_REG]);
	delete [] miRegData;

	stream.read_memory_buffer(MEM_PI_REG); //, 0x84600000);
	stream.read_memory_buffer(MEM_RI_REG); //, 0x84700000);
	stream >> value; Memory_SI_SetRegister( SI_DRAM_ADDR_REG, value );
	stream >> value; Memory_SI_SetRegister( SI_PIF_ADDR_RD64B_REG, value );
	stream >> value; Memory_SI_SetRegister( SI_PIF_ADDR_WR64B_REG, value );
	stream >> value; Memory_SI_SetRegister( SI_STATUS_REG, value );
	for(i = 0; i < 32; i++)
	{
		stream.skip(4); // boolean that tells whether the entry is defined - seems redundant
		int pagemask, hi, lo0, lo1;
		stream >> pagemask;
		stream >> hi;
		stream >> lo0;
		stream >> lo1;

		g_TLBs[i].UpdateValue(pagemask, hi, lo1, lo0);
	}
	for(i = 0; i < 32; i++)
	{
		if(i == C0_SR)
		{
			R4300_SetSR(g_dwNewCPR0[i]);
		}
		else if(i == C0_COMPARE)
		{
			CPU_SetCompare(g_dwNewCPR0[i]);
		}
		else
		{
			gCPUState.CPUControl[i]._u32 = g_dwNewCPR0[i];
		}
	}
	//stream.skip(0x40);

	stream.read(gMemBuffers[MEM_PIF_RAM], 0x40);
	Swap_PIF();

	stream.read(gMemBuffers[MEM_RD_RAM], gRamSize);
	stream.read_memory_buffer(MEM_SP_MEM); //, 0x84000000);

#ifdef DAEDALUS_ENABLE_OS_HOOKS
	OSHLE_PatchAll();
#endif

	return true;
}

// static RomID GetRomIDFromSaveState( const std::string& filename )
// {
// 	SaveStateInput stream( filename );

// 	if( !stream.IsValid() )
// 	{
// 		return RomID();
// 	}

// 	u32 value;
// 	stream >> value;
// 	if(value != SAVESTATE_PROJECT64_MAGIC_NUMBER)
// 		return RomID();

// 	u32 ram_size;
// 	stream >> ram_size;

// 	ROMHeader rom_header;
// 	stream >> rom_header;
// 	ROMFile::ByteSwap_3210(&rom_header, 64);

// 	return RomID( rom_header.CRC1, rom_header.CRC2, rom_header.CountryID );
// }

static const char* GetRomFromSaveState( const std::string& filename )
{
	SaveStateInput stream( filename );

	if( !stream.IsValid() ) {
		return NULL;
	}

	u32 value;
	stream >> value;
	if(value != SAVESTATE_PROJECT64_MAGIC_NUMBER)
		return NULL;

	u32 ram_size;
	stream >> ram_size;

	ROMHeader rom_header;
	stream >> rom_header;
	ROMFile::ByteSwap_3210(&rom_header, 64);

	return CRomDB::Get()->QueryFilenameFromID(
		RomID( rom_header.CRC1, rom_header.CRC2, rom_header.CountryID ));
}

bool SaveState_RequestSave(const std::string& filename)
{
	// Call SaveStateToFile directly if the CPU is not running.
	DAEDALUS_ASSERT(CPU_IsRunning(), "Expecting the CPU to be running at this point");

	MutexLock lock(&gSaveStateMutex);

	// Abort if already in the process of loading/saving
	if (gSaveStateOperation != SSO_NONE)
	{
		return false;
	}

	gSaveStateOperation = SSO_SAVE;
	gSaveStateFilename = filename;
	gCPUState.AddJob(CPU_CHANGE_CORE);

	return true;
}

bool SaveState_RequestLoad(const std::string& filename)
{
	// Call SaveStateToFile directly if the CPU is not running.
	DAEDALUS_ASSERT(CPU_IsRunning(), "Expecting the CPU to be running at this point");

	MutexLock lock(&gSaveStateMutex);

	// Abort if already in the process of loading/saving
	if (gSaveStateOperation != SSO_NONE)
	{
		return false;
	}

	gSaveStateOperation = SSO_LOAD;
	gSaveStateFilename = filename;
	gCPUState.AddJob(CPU_CHANGE_CORE);

	return true;  // XXXX could fail
}

static void HandleSaveStateOperationOnVerticalBlank()
{
	DAEDALUS_ASSERT(CPU_IsRunning(), "Expecting the CPU to be running at this point");

	if (gSaveStateOperation == SSO_NONE) return;

	MutexLock lock(&gSaveStateMutex);

	// Handle the save state
	switch (gSaveStateOperation)
	{
		case SSO_NONE:
			DAEDALUS_ERROR("Unreachable");
			break;
		case SSO_SAVE:
			Console_Print("Saving '%s'\n", gSaveStateFilename.c_str());
			SaveStateToFile(gSaveStateFilename);
			gSaveStateOperation = SSO_NONE;
			break;
		case SSO_LOAD:
			Console_Print("Loading '%s'\n", gSaveStateFilename.c_str());

			// Try to load the savestate immediately. If this fails, it
			// usually means that we're running the correct rom (we should have a
			// separate return code to check this case). In that case we
			// stop the cpu and handle the load in
			// HandleSaveStateOperationOnCPUStopRunning.
			if (LoadStateFromFile(gSaveStateFilename))
			{
				CPU_ResetFragmentCache();
				gSaveStateOperation = SSO_NONE;
			}
			else
			{
				// Halt the CPU so that we can swap the rom safely and load the savesate.
				CPU_Halt("Load SaveSate");
				// NB: return without clearing gSaveStateOperation
			}
			break;
	}
}

// Returns true if we handled a load request and should keep running.
static bool HandleSaveStateOperationOnCPUStopRunning()
{
	if (gSaveStateOperation != SSO_LOAD) return false;

	MutexLock lock(&gSaveStateMutex);

	gSaveStateOperation = SSO_NONE;

	if (const char* rom_filename = GetRomFromSaveState(gSaveStateFilename.c_str()))
	{
		System_Close();
		System_Open(rom_filename);
		LoadStateFromFile(gSaveStateFilename.c_str());
	}
	else
	{
		Console_Print("Couldn't find matching rom for %s\n", gSaveStateFilename.c_str());
		// Keep running with the current rom.
	}

	return true;
}


class SaveStateCpuEventHandler : public CpuEventHandler
{
	void OnVerticalBlank() override
	{
		HandleSaveStateOperationOnVerticalBlank();
	}

	bool OnCpuStopped() override
	{
		return HandleSaveStateOperationOnCPUStopRunning();
	}
};
static SaveStateCpuEventHandler gSaveStateEventHandler;



bool SaveState_Initialise()
{
	CPU_RegisterCpuEventHandler(&gSaveStateEventHandler);
	return true;
}

void SaveState_Finalise()
{
	CPU_UnregisterCpuEventHandler(&gSaveStateEventHandler);
}
