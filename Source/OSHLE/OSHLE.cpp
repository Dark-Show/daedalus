/*
Copyright (C) 2001 StrmnNrmn

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
#include "OSHLE/OSHLE.h"
#include <stddef.h>  // offsetof
#include "Config/ConfigOptions.h"
#include "Core/CPU.h"
#include "Core/DMA.h"
#include "Core/Memory.h"
#include "Core/R4300.h"
#include "Core/ROM.h"
#include "Core/Registers.h"
#include "Core/Save.h"
#include "Debug/Console.h"
#include "Debug/DebugLog.h"
#include "Debug/Dump.h"
#include "DynaRec/Fragment.h"
#include "DynaRec/FragmentCache.h"
#include "HLEAudio/AudioPlugin.h"
#include "Math/Math.h"
#include "OSHLE/OS.h"
#include "OSHLE/OSMesgQueue.h"
#include "OSHLE/PatchTables.h"
#include "OSHLE/patch_symbols.h"
#include "System/Endian.h"
#include "System/IO.h"
#include "Ultra/ultra_R4300.h"
#include "Ultra/ultra_os.h"
#include "Ultra/ultra_rcp.h"
#include "Ultra/ultra_sptask.h"
#include "Utility/CRC.h"
#include "Utility/FastMemcpy.h"
#include "Utility/Profiler.h"

#ifdef DAEDALUS_ENABLE_OS_HOOKS

#ifdef DUMPOSFUNCTIONS
static const char* const gEventStrings[23] = {
	"OS_EVENT_SW1",
	"OS_EVENT_SW2",
	"OS_EVENT_CART",
	"OS_EVENT_COUNTER",
	"OS_EVENT_SP",
	"OS_EVENT_SI",
	"OS_EVENT_AI",
	"OS_EVENT_VI",
	"OS_EVENT_PI",
	"OS_EVENT_DP",
	"OS_EVENT_CPU_BREAK",
	"OS_EVENT_SP_BREAK",
	"OS_EVENT_FAULT",
	"OS_EVENT_THREADSTATUS",
	"OS_EVENT_PRENMI",
	"OS_EVENT_RDB_READ_DONE",
	"OS_EVENT_RDB_LOG_DONE",
	"OS_EVENT_RDB_DATA_DONE",
	"OS_EVENT_RDB_REQ_RAMROM",
	"OS_EVENT_RDB_FREE_RAMROM",
	"OS_EVENT_RDB_DBG_DONE",
	"OS_EVENT_RDB_FLUSH_PROF",
	"OS_EVENT_RDB_ACK_PROF",
};
#endif  // DUMPOSFUNCTIONS

u32 gNumOSFunctions;

#define TEST_DISABLE_FUNCS  // return PATCH_RET_NOT_PROCESSED;

#define PATCH_RET_NOT_PROCESSED RET_NOT_PROCESSED(NULL)
#define PATCH_RET_NOT_PROCESSED0(name) RET_NOT_PROCESSED(&PATCH_SYMBOL_FUNCTION_ENTRY(name))
#define PATCH_RET_JR_RA RET_JR_RA()
#define PATCH_RET_ERET RET_JR_ERET()

// Increase this number every time we changed the symbol table
static const u32 MAGIC_HEADER = 0x80000151;

static bool gPatchesApplied = false;

// u32 g_dwOSStart = 0x00240000;
// u32 g_dwOSEnd   = 0x00380000;

static void OSHLE_ResetSymbolTable();
static void OSHLE_RecurseAndFind();
static bool OSHLE_LocateFunction(PatchSymbol* ps);
static bool OSHLE_VerifyLocation(PatchSymbol* ps, u32 index);
static bool OSHLE_VerifyLocation_CheckSignature(PatchSymbol* ps, PatchSignature* psig, u32 index);
static bool OSHLE_LoadCache();
static void OSHLE_FlushCache();

static void OSHLE_ApplyPatches();
static void OSHLE_ApplyPatch(u32 i);
static u32 gNumPatchSymbols;
static u32 gNumPatchVariables;

class PatchDMAEventHandler : public DMAEventHandler
{
	void OnRomCopied() override
	{
		// Note the rom is only scanned when the ROM jumps to the game boot address
		// ToDO: try to reapply patches - certain roms load in more of the OS after a number of transfers ?
		OSHLE_ApplyPatches();
	}
};
static PatchDMAEventHandler gPatchDMAEventHandler;


bool OSHLE_Initialise()
{
	DMA_RegisterDMAEventHandler(&gPatchDMAEventHandler);
	return true;
}

void OSHLE_Finalise()
{
	DMA_UnregisterDMAEventHandler(&gPatchDMAEventHandler);
}

void OSHLE_Reset()
{
	gPatchesApplied = false;
	gNumOSFunctions = 0;
	OSHLE_ResetSymbolTable();
}

void OSHLE_ResetSymbolTable()
{
	u32 i = 0;
	// Loops through all symbols, until name is null
	for (i = 0; gPatchSymbols[i] != NULL; i++)
	{
		gPatchSymbols[i]->Found = false;
	}
	gNumPatchSymbols = i;

	for (i = 0; gPatchVariables[i] != NULL; i++)
	{
		gPatchVariables[i]->Found = false;
		gPatchVariables[i]->FoundHi = false;
		gPatchVariables[i]->FoundLo = false;
	}
	gNumPatchVariables = i;
}

void OSHLE_ApplyPatches()
{
	gPatchesApplied = true;

	if (!gOSHooksEnabled)
		return;

	if (!OSHLE_LoadCache())
	{
		OSHLE_RecurseAndFind();

		// Tip : Disable this when working on oshle funcs, you save the time to delete
		// hle cache everyttime you need to test :p
		OSHLE_FlushCache();
	}

	// Do this every time or just when originally patched
	/*result = */ OS_Reset();
}

void OSHLE_PatchAll()
{
	gNumOSFunctions = 0;

	if (!gPatchesApplied)
	{
		OSHLE_ApplyPatches();
	}
#ifdef DUMPOSFUNCTIONS
	FILE* fp;
	std::string path = IO::Path::Join(Dump_GetDumpDirectory(""), "n64.cfg");
	fp = fopen(path.c_str(), "w");
#endif
	for (u32 i = 0; i < gNumPatchSymbols; i++)
	{
		if (gPatchSymbols[i]->Found)
		{
#ifdef DUMPOSFUNCTIONS
			PatchSymbol* ps = gPatchSymbols[i];
			std::string buf = IO::Path::Join(Dump_GetDumpDirectory("oshle"), ps->Name);

			Dump_Disassemble(PHYS_TO_K0(ps->Location),
							 PHYS_TO_K0(ps->Location) + ps->Signatures->NumOps * sizeof(OpCode), buf);

			fprintf(fp, "%s 0x%08x\n", ps->Name, PHYS_TO_K0(ps->Location));
#endif
			gNumOSFunctions++;
			OSHLE_ApplyPatch(i);
		}
	}
#ifdef DUMPOSFUNCTIONS
	fclose(fp);
#endif
}

void OSHLE_ApplyPatch(u32 i)
{
#ifdef DAEDALUS_ENABLE_DYNAREC
	u32 pc = gPatchSymbols[i]->Location;

	CFragment* frag = new CFragment(gFragmentCache.GetCodeBufferManager(), PHYS_TO_K0(pc),
									gPatchSymbols[i]->Signatures->NumOps, (void*)gPatchSymbols[i]->Function);

	gFragmentCache.InsertFragment(frag);
#endif
}

// Return the location of a symbol
static u32 OSHLE_GetSymbolAddress(const char* name)
{
	// Search new list
	for (u32 p = 0; p < gNumPatchSymbols; p++)
	{
		if (gPatchSymbols[p]->Found && _strcmpi(gPatchSymbols[p]->Name, name) == 0)
		{
			return PHYS_TO_K0(gPatchSymbols[p]->Location);
		}
	}

	// The patch was not found
	return u32(~0);
}

// Given a location, this function returns the name of the matching
// symbol (if there is one)

const char* OSHLE_GetJumpAddressName(u32 jump)
{
	const void* mem_base;
	if (!Memory_GetInternalReadAddress(jump, &mem_base))
		return "??";

	// Search new list
	for (u32 p = 0; p < gNumPatchSymbols; p++)
	{
		// Skip symbol if already found.
		if (!gPatchSymbols[p]->Found)
			continue;

		const void* patch_base = gu32RamBase + (gPatchSymbols[p]->Location / 4);

		// Symbol not found, attempt to locate on this pass. This may
		// fail if all dependent symbols are not found
		if (patch_base == mem_base)
		{
			return gPatchSymbols[p]->Name;
		}
	}

	// The patch was not found
	return "?";
}

#ifdef DUMPOSFUNCTIONS

void OSHLE_DumpOsThreadInfo()
{
	u32 dwCurrentThread = Read32Bits(VAR_ADDRESS(osActiveThread));
	u32 dwFirstThread = Read32Bits(VAR_ADDRESS(osGlobalThreadList));
	u32 dwThread = dwFirstThread;

	Console_Print("");
	Console_Print("Threads:      Pri   Queue       State   Flags   ID          FP Used");
	// Console_Print("  0x01234567, xxxx, 0x01234567, 0x0123, 0x0123, 0x01234567,
	// 0x01234567",
	while (dwThread)
	{
		u32 dwPri = Read32Bits(dwThread + offsetof(OSThread, priority));
		u32 dwQueue = Read32Bits(dwThread + offsetof(OSThread, queue));
		u16 wState = Read16Bits(dwThread + offsetof(OSThread, state));
		u16 wFlags = Read16Bits(dwThread + offsetof(OSThread, flags));
		u32 dwID = Read32Bits(dwThread + offsetof(OSThread, id));
		u32 dwFP = Read32Bits(dwThread + offsetof(OSThread, fp));

		// Hack to avoid null thread
		if (dwPri == 0xFFFFFFFF)
			break;

		if (dwThread == dwCurrentThread)
		{
			Console_Print("->0x%08x, % 4d, 0x%08x, 0x%04x, 0x%04x, 0x%08x, 0x%08x", dwThread, dwPri, dwQueue, wState,
						  wFlags, dwID, dwFP);
		}
		else
		{
			Console_Print("  0x%08x, % 4d, 0x%08x, 0x%04x, 0x%04x, 0x%08x, 0x%08x", dwThread, dwPri, dwQueue, wState,
						  wFlags, dwID, dwFP);
		}
		dwThread = Read32Bits(dwThread + offsetof(OSThread, tlnext));

		if (dwThread == dwFirstThread)
			break;
	}
}

void OSHLE_DumpOsQueueInfo()
{
#ifdef DAED_OS_MESSAGE_QUEUES
	Console_Print("There are %d Queues", g_MessageQueues.size());
	Console_Print("Queues:   Empty     Full      Valid First MsgCount Msg");
	// Console_Print("01234567, 01234567, 01234567, xxxx, xxxx, xxxx, 01234567",
	for (u32 queue : g_MessageQueues)
	{
		char fullqueue_buffer[30];
		char emptyqueue_buffer[30];
		char type_buffer[60] = "";

		COSMesgQueue q(queue);

		u32 dwEmptyQ = q.GetEmptyQueue();
		u32 dwFullQ = q.GetFullQueue();
		u32 dwValidCount = q.GetValidCount();
		u32 dwFirst = q.GetFirst();
		u32 dwMsgCount = q.GetMsgCount();
		u32 dwMsg = q.GetMesgArray();

		if ((s32)dwFirst < 0 || (s32)dwValidCount < 0 || (s32)dwMsgCount < 0)
		{
			continue;
		}

		if (dwFullQ == VAR_ADDRESS(osNullMsgQueue))
			sprintf(fullqueue_buffer, "       -");
		else
			sprintf(fullqueue_buffer, "%08x", dwFullQ);

		if (dwEmptyQ == VAR_ADDRESS(osNullMsgQueue))
			sprintf(emptyqueue_buffer, "       -");
		else
			sprintf(emptyqueue_buffer, "%08x", dwEmptyQ);

		if (queue == VAR_ADDRESS(osSiAccessQueue))
		{
			sprintf(type_buffer, "<- Si Access");
		}
		else if (queue == VAR_ADDRESS(osPiAccessQueue))
		{
			sprintf(type_buffer, "<- Pi Access");
		}

		// Try and find in the event mesg array
		if (strlen(type_buffer) == 0 && VAR_FOUND(osEventMesgArray))
		{
			for (u32 j = 0; j < 23; j++)
			{
				if (queue == Read32Bits(VAR_ADDRESS(osEventMesgArray) + (j * 8) + 0x0))
				{
					sprintf(type_buffer, "<- %s", gEventStrings[j]);
					break;
				}
			}
		}
		Console_Print("%08x, %s, %s, % 4d, % 4d, % 4d, %08x %s", queue, emptyqueue_buffer, fullqueue_buffer,
					  dwValidCount, dwFirst, dwMsgCount, dwMsg, type_buffer);
	}
#endif
}

void OSHLE_DumpOsEventInfo()
{
	if (!VAR_FOUND(osEventMesgArray))
	{
		Console_Print("osSetEventMesg not patched, event table unknown");
		return;
	}

	Console_Print("");
	Console_Print("Events:                      Queue      Message");
	for (u32 i = 0; i < 23; i++)
	{
		u32 dwQueue = Read32Bits(VAR_ADDRESS(osEventMesgArray) + (i * 8) + 0x0);
		u32 dwMsg = Read32Bits(VAR_ADDRESS(osEventMesgArray) + (i * 8) + 0x4);

		Console_Print("  %-26s 0x%08x 0x%08x", gEventStrings[i], dwQueue, dwMsg);
	}
}

#endif  // DUMPOSFUNCTIONS

bool OSHLE_Hacks(PatchSymbol* ps)
{
	bool found = false;

	// Hacks to disable certain os funcs in games that causes issues
	// This alot cheaper than adding a check on the func itself, this is only checked once -Salvy
	// Eventually we should fix them though
	//
	// osSendMesg - Breaks the in game menu in Zelda OOT
	// osSendMesg - Causes Animal Corssing to freeze after the N64 logo
	// osSendMesg - Causes Clay Fighter 63 1-3 to not boot
	//
	switch (g_ROM.GameHacks)
	{
		case ZELDA_OOT:
		case ANIMAL_CROSSING:
		case CLAY_FIGHTER_63:

			if (strcmp("osSendMesg", ps->Name) == 0)
			{
				found = true;
				break;
			}
			break;

		//
		// __osDispatchThread and __osEnqueueAndYield causes Body Harvest to not boot
		// This game is very sensitive with IRQs, see DMA.cpp (DMA_SI_CopyToDRAM)
		case BODY_HARVEST:
			if (strcmp("__osDispatchThread", ps->Name) == 0)
			{
				found = true;
				break;
			}
			if (strcmp("__osEnqueueAndYield", ps->Name) == 0)
			{
				found = true;
				break;
			}
			break;
		default:
			break;
	}

	return found;
}

// ToDo: Add Status bar for loading OSHLE Patch Symbols.
void OSHLE_RecurseAndFind()
{
	Console_Print("Searching for os functions. This may take several seconds...");

	// Keep looping until a pass does not resolve any more symbols
	s32 num_found = 0;

	Console_OverwriteStart();

	// Loops through all symbols, until name is null
	for (u32 i = 0; i < gNumPatchSymbols && !gCPUState.IsJobSet(CPU_STOP_RUNNING); i++)
	{
		Console_Overwrite("OS HLE: %d / %d Looking for [G%s]", i, gNumPatchSymbols, gPatchSymbols[i]->Name);
		Console_Flush();
		// Skip symbol if already found, or if it is a variable
		if (gPatchSymbols[i]->Found)
			continue;

		// Symbol not found, attempt to locate on this pass. This may
		// fail if all dependent symbols are not found
		if (OSHLE_LocateFunction(gPatchSymbols[i]))
			num_found++;
	}

	if (gCPUState.IsJobSet(CPU_STOP_RUNNING))
	{
		Console_Overwrite("OS HLE: Aborted");
		Console_OverwriteEnd();
		return;
	}
	Console_Overwrite("OS HLE: %d / %d All done", gNumPatchSymbols, gNumPatchSymbols);
	Console_OverwriteEnd();

	u32 first = u32(~0);
	u32 last = 0;

	num_found = 0;
	for (u32 i = 0; i < gNumPatchSymbols; i++)
	{
		if (!gPatchSymbols[i]->Found)
		{
			// Console_Print("[W%s] not found", gPatchSymbols[i]->Name);
		}
		else
		{
			// Find duplicates! (to avoid showing the same clash twice, only scan up to the first symbol)
			bool found_duplicate(false);
			for (u32 j = 0; j < i; j++)
			{
				if (gPatchSymbols[i]->Found && gPatchSymbols[j]->Found &&
					(gPatchSymbols[i]->Location == gPatchSymbols[j]->Location))
				{
					Console_Print("Warning [C%s==%s]", gPatchSymbols[i]->Name, gPatchSymbols[j]->Name);

					// Don't patch!
					gPatchSymbols[i]->Found = false;
					gPatchSymbols[j]->Found = false;
					found_duplicate = true;
					break;
				}
			}
			// Disable certain os funcs where it causes issues in some games ex Zelda
			//
			if (OSHLE_Hacks(gPatchSymbols[i]))
			{
				Console_Print("[ROS Hack : Disabling %s]", gPatchSymbols[i]->Name);
				gPatchSymbols[i]->Found = false;
			}

			if (!found_duplicate)
			{
				u32 location = gPatchSymbols[i]->Location;
				if (location < first)
					first = location;
				if (location > last)
					last = location;

				// Actually patch:
				OSHLE_ApplyPatch(i);
				num_found++;
			}
		}
		Console_Print("%d/%d symbols identified, in range 0x%08x -> 0x%08x", num_found, gNumPatchSymbols, first, last);
	}

	num_found = 0;
	for (u32 i = 0; i < gNumPatchVariables; i++)
	{
		if (!gPatchVariables[i]->Found)
		{
			// Console_Print("[W%s] not found", gPatchVariables[i]->Name);
		}
		else
		{
			// Find duplicates! (to avoid showing the same clash twice, only scan up to the first symbol)
			for (u32 j = 0; j < i; j++)
			{
				if (gPatchVariables[i]->Found && gPatchVariables[j]->Found &&
					(gPatchVariables[i]->Location == gPatchVariables[j]->Location))
				{
					Console_Print("Warning [C%s==%s]", gPatchVariables[i]->Name, gPatchVariables[j]->Name);
				}
			}

			num_found++;
		}
		Console_Print("%d/%d variables identified", num_found, gNumPatchVariables);
	}
}

// Attempt to locate this symbol.
bool OSHLE_LocateFunction(PatchSymbol* ps)
{
	const u32* code_base = gu32RamBase;

	for (u32 s = 0; s < ps->Signatures[s].NumOps; s++)
	{
		PatchSignature* psig = &ps->Signatures[s];

		// Sweep through OS range
		for (u32 i = 0; i < (gRamSize >> 2); i++)
		{
			OpCode op;
			op._u32 = code_base[i];
			op = GetCorrectOp(op);

			// First op must match!
			if (psig->FirstOp != op.op)
				continue;

			// See if function i exists at this location
			if (OSHLE_VerifyLocation_CheckSignature(ps, psig, i))
			{
				return true;
			}
		}
	}

	return false;
}

#define JumpTarget(op, addr) (((addr)&0xF0000000) | (op.target << 2))

// Check that the function i is located at address index
bool OSHLE_VerifyLocation(PatchSymbol* ps, u32 index)
{
	// We may have already located this symbol.
	if (ps->Found)
	{
		// The location must match!
		return (ps->Location == (index << 2));
	}

	// Fail if index is outside of indexable memory
	if (index > gRamSize >> 2)
		return false;

	for (u32 s = 0; s < ps->Signatures[s].NumOps; s++)
	{
		if (OSHLE_VerifyLocation_CheckSignature(ps, &ps->Signatures[s], index))
		{
			return true;
		}
	}

	// Not found!
	return false;
}

bool OSHLE_VerifyLocation_CheckSignature(PatchSymbol* ps, PatchSignature* psig, u32 index)
{
	PatchCrossRef* pcr = psig->CrossRefs;
	bool cross_ref_var_set = false;

	if ((index + psig->NumOps) * 4 > gRamSize)
	{
		return false;
	}

	const u32* code_base = gu32RamBase;

	PatchCrossRef dummy_cr = {static_cast<u32>(~0), PX_JUMP, NULL};

	if (pcr == NULL)
		pcr = &dummy_cr;

	u32 last = pcr->Offset;
	u32 crc = 0;
	u32 partial_crc = 0;
	for (u32 m = 0; m < psig->NumOps; m++)
	{
		// Get the actual opcode at this address, not patched/compiled code
		OpCode op;
		op._u32 = code_base[index + m];
		op = GetCorrectOp(op);
		// This should be ok - so long as we patch all functions at once.

		// Check if a cross reference is in effect here
		if (pcr->Offset == m)
		{
			// This is a cross reference.
			switch (pcr->Type)
			{
				case PX_JUMP:
				{
					u32 TargetIndex = JumpTarget(op, (index + m) << 2) >> 2;

					// If the opcode at this address is not a Jump/Jal then
					// this can't match
					if (op.op != OP_JAL && op.op != OP_J)
						goto fail_find;

					// This is a jump, the jump target must match the
					// symbol pointed to by this function. Recurse
					if (!OSHLE_VerifyLocation(pcr->Symbol, TargetIndex))
						goto fail_find;

					op.target = 0;  // Mask out jump location
				}
				break;
				case PX_VARIABLE_HI:
				{
					// The data element should be consistant with the symbol
					if (pcr->Variable->FoundHi)
					{
						if (pcr->Variable->HiWord != (op._u32 & 0xFFFF))
							goto fail_find;
					}
					else
					{
						// Assume this is the correct symbol
						pcr->Variable->FoundHi = true;
						pcr->Variable->HiWord = (u16)(op._u32 & 0xFFFF);

						cross_ref_var_set = true;

						// If other half has been identified, set the location
						if (pcr->Variable->FoundLo)
							pcr->Variable->Location = (pcr->Variable->HiWord << 16) + (short)(pcr->Variable->LoWord);
					}

					op._u32 &= ~0x0000ffff;  // Mask out low halfword
				}
				break;
				case PX_VARIABLE_LO:
				{
					// The data element should be consistant with the symbol
					if (pcr->Variable->FoundLo)
					{
						if (pcr->Variable->LoWord != (op._u32 & 0xFFFF))
							goto fail_find;
					}
					else
					{
						// Assume this is the correct symbol
						pcr->Variable->FoundLo = true;
						pcr->Variable->LoWord = (u16)(op._u32 & 0xFFFF);

						cross_ref_var_set = true;

						// If other half has been identified, set the location
						if (pcr->Variable->FoundHi)
							pcr->Variable->Location = (pcr->Variable->HiWord << 16) + (short)(pcr->Variable->LoWord);
					}

					op._u32 &= ~0x0000ffff;  // Mask out low halfword
				}
				break;
			}

			// We've handled this cross ref - point to the next one
			// ready for the next match.
			pcr++;

			// If pcr->Offset == ~0, then there are no more in the array
			// This is okay, as the comparison with m above will never match
			DAEDALUS_ASSERT(pcr->Offset >= last, "%s: CrossReference offsets out of order", ps->Name);
			last = pcr->Offset;
		}
		else
		{
			if (op.op == OP_J)
			{
				op.target = 0;  // Mask out jump location
			}
		}

		// If this is currently less than 4 ops in, add to the partial crc
		if (m < PATCH_PARTIAL_CRC_LEN)
			partial_crc = daedalus_crc32(partial_crc, (u8*)&op, 4);

		// Here, check the partial crc if m == 3
		if (m == (PATCH_PARTIAL_CRC_LEN - 1))
		{
			if (partial_crc != psig->PartialCRC)
			{
				goto fail_find;
			}
		}

		// Add to the total crc
		crc = daedalus_crc32(crc, (u8*)&op, 4);
	}

	// Check if the complete crc matches!
	if (crc != psig->CRC)
	{
		goto fail_find;
	}

	// We have located the symbol
	ps->Found = true;
	ps->Location = index << 2;
	ps->Function = psig->Function;  // Install this function

	if (cross_ref_var_set)
	{
		// Loop through symbols, setting variables if both high/low found
		for (pcr = psig->CrossRefs; pcr->Offset != u32(~0); pcr++)
		{
			if (pcr->Type == PX_VARIABLE_HI || pcr->Type == PX_VARIABLE_LO)
			{
				if (pcr->Variable->FoundLo && pcr->Variable->FoundHi)
				{
					pcr->Variable->Found = true;
				}
			}
		}
	}

	return true;

// Goto - ugh
fail_find:

	// Loop through symbols, clearing variables if they have been set
	if (cross_ref_var_set)
	{
		for (pcr = psig->CrossRefs; pcr->Offset != u32(~0); pcr++)
		{
			if (pcr->Type == PX_VARIABLE_HI || pcr->Type == PX_VARIABLE_LO)
			{
				if (!pcr->Variable->Found)
				{
					pcr->Variable->FoundLo = false;
					pcr->Variable->FoundHi = false;
				}
			}
		}
	}

	return false;
}

static void OSHLE_FlushCache()
{
	std::string name = Save_GetDirectory(".hle");

	FILE* fp = fopen(name.c_str(), "wb");
	if (!fp)
	{
		Console_Print("Failed to write OSHLE cache: %s", name.c_str());
		return;
	}

	Console_Print("Write OSHLE cache: %s", name.c_str());

	u32 data = MAGIC_HEADER;
	fwrite(&data, 1, sizeof(data), fp);

	for (u32 i = 0; i < gNumPatchSymbols; i++)
	{
		if (gPatchSymbols[i]->Found)
		{
			data = gPatchSymbols[i]->Location;
			fwrite(&data, 1, sizeof(data), fp);
			for (data = 0;; data++)
			{
				if (gPatchSymbols[i]->Signatures[data].Function == gPatchSymbols[i]->Function)
					break;
			}
			fwrite(&data, 1, sizeof(data), fp);
		}
		else
		{
			data = 0;
			fwrite(&data, 1, sizeof(data), fp);
		}
	}

	for (u32 i = 0; i < gNumPatchVariables; i++)
	{
		if (gPatchVariables[i]->Found)
		{
			data = gPatchVariables[i]->Location;
		}
		else
		{
			data = 0;
		}

		fwrite(&data, 1, sizeof(data), fp);
	}

	fclose(fp);
	Console_Print("Wrote OSHLE cache: %s", name.c_str());
}

static bool OSHLE_LoadCache()
{
	std::string name = Save_GetDirectory(".hle");
	FILE* fp = fopen(name.c_str(), "rb");
	if (!fp)
	{
		return false;
	}

	Console_Print("Read from OSHLE cache: %s", name.c_str());

	u32 data;
	fread(&data, 1, sizeof(data), fp);
	if (data != MAGIC_HEADER)
	{
		fclose(fp);
		return false;
	}

	for (u32 i = 0; i < gNumPatchSymbols; i++)
	{
		fread(&data, 1, sizeof(data), fp);
		if (data != 0)
		{
			gPatchSymbols[i]->Found = true;
			gPatchSymbols[i]->Location = data;
			fread(&data, 1, sizeof(data), fp);
			gPatchSymbols[i]->Function = gPatchSymbols[i]->Signatures[data].Function;
		}
		else
			gPatchSymbols[i]->Found = false;
	}

	for (u32 i = 0; i < gNumPatchVariables; i++)
	{
		fread(&data, 1, sizeof(data), fp);
		if (data != 0)
		{
			gPatchVariables[i]->Found = true;
			gPatchVariables[i]->Location = data;
		}
		else
		{
			gPatchVariables[i]->Found = false;
			gPatchVariables[i]->Location = 0;
		}
	}

	fclose(fp);
	Console_Print("Read completed from OSHLE cache: %s", name.c_str());
	return true;
}

static u32 RET_NOT_PROCESSED(PatchSymbol* ps)
{
	DAEDALUS_ASSERT(ps != NULL, "Not Supported");

	gCPUState.CurrentPC = PHYS_TO_K0(ps->Location);
	// Console_Print("%s RET_NOT_PROCESSED PC=0x%08x RA=0x%08x", ps->Name, gCPUState.TargetPC, gGPR[REG_ra]._u32_0);

	gCPUState.Delay = NO_DELAY;
	gCPUState.TargetPC = gCPUState.CurrentPC;

	// Simulate the first op then return to dynarec. so we still can leverage dynarec.
	OpCode op_code;
	op_code._u32 = Read32Bits(gCPUState.CurrentPC);
	R4300_ExecuteInstruction(op_code);
	DAEDALUS_ASSERT(gCPUState.Delay == NO_DELAY, "OS functions' first op is a JUMP??");
	INCREMENT_PC();
	gCPUState.TargetPC = gCPUState.CurrentPC;

	return 1;
}

inline u32 RET_JR_RA()
{
	gCPUState.TargetPC = gGPR[REG_ra]._u32_0;
	return 1;
}

static u32 RET_JR_ERET()
{
	if (gCPUState.CPUControl[C0_SR]._u32 & SR_ERL)
	{
		// Returning from an error trap
		CPU_SetPC(gCPUState.CPUControl[C0_ERROR_EPC]._u32);
		gCPUState.CPUControl[C0_SR]._u32 &= ~SR_ERL;
	}
	else
	{
		// Returning from an exception
		CPU_SetPC(gCPUState.CPUControl[C0_EPC]._u32);
		gCPUState.CPUControl[C0_SR]._u32 &= ~SR_EXL;
	}
	// Point to previous instruction (as we increment the pointer immediately afterwards
	DECREMENT_PC();

	// Ensure we don't execute this in the delay slot
	gCPUState.Delay = NO_DELAY;

	return 0;
}

static u32 ConvertToPhysical(u32 addr)
{
	DAEDALUS_ASSERT(IS_K0K1(addr) == (IS_KSEG0(addr) | IS_KSEG1(addr)), "IS_K0K1 is inconsistent");

	if (IS_K0K1(addr))
	{
		return K0_TO_PHYS(addr);  // Same as K1_TO_PHYS
	}
	else
	{
		return OS_HLE___osProbeTLB(addr);
	}
}

extern void MemoryUpdateMI(u32 value);
extern void MemoryUpdateSPStatus(u32 flags);

u32 Patch_osStartThread();

#include "patch_ai_hle.inl"
#include "patch_cache_hle.inl"
#include "patch_eeprom_hle.inl"
#include "patch_gu_hle.inl"
#include "patch_math_hle.inl"
#include "patch_mesg_hle.inl"
#include "patch_pi_hle.inl"
#include "patch_regs_hle.inl"
#include "patch_si_hle.inl"
#include "patch_sp_hle.inl"
#include "patch_thread_hle.inl"
#include "patch_timer_hle.inl"
#include "patch_tlb_hle.inl"
#include "patch_util_hle.inl"
#include "patch_vi_hle.inl"

/////////////////////////////////////////////////////

u32 Patch___osContAddressCrc()
{
	TEST_DISABLE_FUNCS
	Console_Print("__osContAddressCrc(0x%08x)", gGPR[REG_a0]._u32_0);
	return PATCH_RET_NOT_PROCESSED;
}

u32 Patch___osPackRequestData()
{
	return PATCH_RET_NOT_PROCESSED;
}

// Perphaps not important for emulation? It works fine when we NOP this
u32 Patch_osSetIntMask()
{
	/*
	//
	// Interrupt masks
	//
	#define	OS_IM_NONE		0x00000001
	#define	OS_IM_SW1		0x00000501
	#define	OS_IM_SW2		0x00000601
	#define	OS_IM_CART		0x00000c01
	#define	OS_IM_PRENMI	0x00001401
	#define OS_IM_RDBWRITE	0x00002401
	#define OS_IM_RDBREAD	0x00004401
	#define	OS_IM_COUNTER	0x00008401
	#define	OS_IM_CPU		0x0000ff01
	#define	OS_IM_SP		0x00010401
	#define	OS_IM_SI		0x00020401
	#define	OS_IM_AI		0x00040401
	#define	OS_IM_VI		0x00080401
	#define	OS_IM_PI		0x00100401
	#define	OS_IM_DP		0x00200401
	#define	OS_IM_ALL		0x003fff01

	#define	RCP_IMASK		0x003f0000
	*/

	// u32 flag   = gGPR[REG_a0]._u32_0;
	// u32 thread = Read32Bits(VAR_ADDRESS(osActiveThread));
	// printf("%08x\n", flag );

	// The interrupt mask is part of the thread context rcp
	// Write32Bits(thread + offsetof(OSThread, context.rcp), flag);

	// Do nothing for now until I can find a test case that won't work ;)
	return PATCH_RET_JR_RA;
}

u32 Patch___osEepromRead_Prepare()
{
	return PATCH_RET_NOT_PROCESSED;
}

u32 Patch___osEepromWrite_Prepare()
{
	return PATCH_RET_NOT_PROCESSED;
}

#include "patch_symbols.inl"

#endif
