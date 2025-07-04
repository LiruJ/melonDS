/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#if defined(__SWITCH__)
#include <switch.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#endif

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <linux/ashmem.h>
#include <sys/ioctl.h>
#endif

#include "ARMJIT.h"
#include "ARMJIT_Memory.h"

#include "ARMJIT_Internal.h"
#include "ARMJIT_Compiler.h"
#include "ARMJIT_Global.h"

#include "DSi.h"
#include "GPU.h"
#include "GPU3D.h"
#include "Wifi.h"
#include "NDSCart.h"
#include "SPU.h"

#include <stdlib.h>

/*
    We're handling fastmem here.

    Basically we're repurposing a big piece of virtual memory
    and map the memory regions as they're structured on the DS
    in it.

    On most systems you have a single piece of main ram,
    maybe some video ram and faster cache RAM and that's about it.
    Here we have not only a lot more different memory regions,
    but also two address spaces. Not only that but they all have
    mirrors (the worst case is 16kb SWRAM which is mirrored 1024x).

    We handle this by only mapping those regions which are actually
    used and by praying the games don't go wild.

    Beware, this file is full of platform specific code and copied
    from Dolphin, so enjoy the copied comments!

*/

// Yes I know this looks messy, but better here than somewhere else in the code
#if defined(__x86_64__)
    #if defined(_WIN32)
        #define CONTEXT_PC Rip
    #elif defined(__linux__)
        #define CONTEXT_PC uc_mcontext.gregs[REG_RIP]
    #elif defined(__APPLE__)
        #define CONTEXT_PC uc_mcontext->__ss.__rip
    #elif defined(__FreeBSD__)
        #define CONTEXT_PC uc_mcontext.mc_rip
    #elif defined(__NetBSD__)
        #define CONTEXT_PC uc_mcontext.__gregs[_REG_RIP]
    #endif
#elif defined(__aarch64__)
    #if defined(_WIN32)
        #define CONTEXT_PC Pc
    #elif defined(__linux__)
        #define CONTEXT_PC uc_mcontext.pc
    #elif defined(__APPLE__)
        #define CONTEXT_PC uc_mcontext->__ss.__pc
    #elif defined(__FreeBSD__)
        #define CONTEXT_PC uc_mcontext.mc_gpregs.gp_elr
    #elif defined(__NetBSD__)
        #define CONTEXT_PC uc_mcontext.__gregs[_REG_PC]
    #endif
#endif

namespace melonDS
{

static constexpr u64 AddrSpaceSize = 0x100000000;
static constexpr u64 VirtmemAreaSize = AddrSpaceSize * 2 + MemoryTotalSize;

using Platform::Log;
using Platform::LogLevel;

#if defined(__ANDROID__)
#define ASHMEM_DEVICE "/dev/ashmem"
#endif

#if defined(__SWITCH__)
// with LTO the symbols seem to be not properly overriden
// if they're somewhere else

void HandleFault(u64 pc, u64 lr, u64 fp, u64 faultAddr, u32 desc);

extern "C"
{

void ARM_RestoreContext(u64* registers) __attribute__((noreturn));

extern char __start__;
extern char __rodata_start;

alignas(16) u8 __nx_exception_stack[0x8000];
u64 __nx_exception_stack_size = 0x8000;

void __libnx_exception_handler(ThreadExceptionDump* ctx)
{
    ARMJIT_Memory::FaultDescription desc;
    u8* curArea = (u8*)(NDS::CurCPU == 0 ? ARMJIT_Memory::FastMem9Start : ARMJIT_Memory::FastMem7Start);
    desc.EmulatedFaultAddr = (u8*)ctx->far.x - curArea;
    desc.FaultPC = (u8*)ctx->pc.x;

    u64 integerRegisters[33];
    memcpy(integerRegisters, &ctx->cpu_gprs[0].x, 8*29);
    integerRegisters[29] = ctx->fp.x;
    integerRegisters[30] = ctx->lr.x;
    integerRegisters[31] = ctx->sp.x;
    integerRegisters[32] = ctx->pc.x;

    if (Melon::FaultHandler(desc))
    {
        integerRegisters[32] = (u64)desc.FaultPC;

        ARM_RestoreContext(integerRegisters);
    }

    HandleFault(ctx->pc.x, ctx->lr.x, ctx->fp.x, ctx->far.x, ctx->error_desc);
}

}

#elif defined(_WIN32)

static LPVOID ExceptionHandlerHandle = nullptr;
static HMODULE KernelBaseDll = nullptr;

using VirtualAlloc2Type = PVOID WINAPI (*)(HANDLE Process, PVOID BaseAddress, SIZE_T Size, ULONG AllocationType, ULONG PageProtection, MEM_EXTENDED_PARAMETER* ExtendedParameters, ULONG ParameterCount);
using MapViewOfFile3Type = PVOID WINAPI (*)(HANDLE FileMapping, HANDLE Process, PVOID BaseAddress, ULONG64 Offset, SIZE_T ViewSize, ULONG AllocationType, ULONG PageProtection, MEM_EXTENDED_PARAMETER* ExtendedParameters, ULONG ParameterCount);

static VirtualAlloc2Type virtualAlloc2Ptr;
static MapViewOfFile3Type mapViewOfFile3Ptr;

LONG ARMJIT_Memory::ExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
{
    if (exceptionInfo->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    u8* curArea = (u8*)(NDS::Current->CurCPU == 0 ? NDS::Current->JIT.Memory.FastMem9Start : NDS::Current->JIT.Memory.FastMem7Start);
    FaultDescription desc {};
    desc.EmulatedFaultAddr = (u8*)exceptionInfo->ExceptionRecord->ExceptionInformation[1] - curArea;
    desc.FaultPC = (u8*)exceptionInfo->ContextRecord->CONTEXT_PC;

    if (FaultHandler(desc, *NDS::Current))
    {
        exceptionInfo->ContextRecord->CONTEXT_PC = (u64)desc.FaultPC;
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    Log(LogLevel::Debug, "it all returns to nothing\n");
    return EXCEPTION_CONTINUE_SEARCH;
}

#else

static struct sigaction OldSaSegv;
static struct sigaction OldSaBus;

void ARMJIT_Memory::SigsegvHandler(int sig, siginfo_t* info, void* rawContext)
{
    if (sig != SIGSEGV && sig != SIGBUS)
    {
        // We are not interested in other signals - handle it as usual.
        return;
    }
    if (info->si_code != SEGV_MAPERR && info->si_code != SEGV_ACCERR)
    {
        // Huh? Return.
        return;
    }

    ucontext_t* context = (ucontext_t*)rawContext;

    FaultDescription desc {};
    u8* curArea = (u8*)(NDS::Current->CurCPU == 0 ? NDS::Current->JIT.Memory.FastMem9Start : NDS::Current->JIT.Memory.FastMem7Start);

    desc.EmulatedFaultAddr = (u8*)info->si_addr - curArea;
    desc.FaultPC = (u8*)context->CONTEXT_PC;

    if (FaultHandler(desc, *NDS::Current))
    {
        context->CONTEXT_PC = (u64)desc.FaultPC;
        return;
    }

    struct sigaction* oldSa;
    if (sig == SIGSEGV)
      oldSa = &OldSaSegv;
    else
      oldSa = &OldSaBus;

    if (oldSa->sa_flags & SA_SIGINFO)
    {
      oldSa->sa_sigaction(sig, info, rawContext);
      return;
    }
    if (oldSa->sa_handler == SIG_DFL)
    {
      signal(sig, SIG_DFL);
      return;
    }
    if (oldSa->sa_handler == SIG_IGN)
    {
      // Ignore signal
      return;
    }
    oldSa->sa_handler(sig);
}

#endif

const u32 OffsetsPerRegion[ARMJIT_Memory::memregions_Count] =
{
    UINT32_MAX,
    UINT32_MAX,
    MemBlockDTCMOffset,
    UINT32_MAX,
    MemBlockMainRAMOffset,
    MemBlockSWRAMOffset,
    UINT32_MAX,
    UINT32_MAX,
    UINT32_MAX,
    MemBlockARM7WRAMOffset,
    UINT32_MAX,
    UINT32_MAX,
    UINT32_MAX,
    UINT32_MAX,
    UINT32_MAX,
    MemBlockNWRAM_AOffset,
    MemBlockNWRAM_BOffset,
    MemBlockNWRAM_COffset
};

enum
{
    memstate_Unmapped,
    memstate_MappedRW,
    // on Switch this is unmapped as well
    memstate_MappedProtected,
};

#define CHECK_ALIGNED(value) assert(((value) & (PageSize-1)) == 0)

bool ARMJIT_Memory::MapIntoRange(u32 addr, u32 num, u32 offset, u32 size) noexcept
{
    CHECK_ALIGNED(addr);
    CHECK_ALIGNED(offset);
    CHECK_ALIGNED(size);

    u8* dst = (u8*)(num == 0 ? FastMem9Start : FastMem7Start) + addr;
#ifdef __SWITCH__
    Result r = (svcMapProcessMemory(dst, envGetOwnProcessHandle(),
        (u64)(MemoryBaseCodeMem + offset), size));
    return R_SUCCEEDED(r);
#elif defined(_WIN32)
    uintptr_t uintptrDst = reinterpret_cast<uintptr_t>(dst);
    for (auto it = VirtmemPlaceholders.begin(); it != VirtmemPlaceholders.end(); it++)
    {
        if (uintptrDst >= it->Start && uintptrDst+size <= it->Start+it->Size)
        {
            //Log(LogLevel::Debug, "found mapping %llx %llx %llx %llx\n", uintptrDst, size, it->Start, it->Size);
            // we split this place holder so that we have a fitting place holder for the mapping
            if (uintptrDst != it->Start || size != it->Size)
            {
                if (!VirtualFree(dst, size, MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER))
                {
                    Log(LogLevel::Debug, "VirtualFree failed with %x\n", GetLastError());
                    return false;
                }
            }

            VirtmemPlaceholder splitPlaceholder = *it;
            VirtmemPlaceholders.erase(it);
            if (uintptrDst > splitPlaceholder.Start)
            {
                //Log(LogLevel::Debug, "splitting on the left %llx\n", uintptrDst - splitPlaceholder.Start);
                VirtmemPlaceholders.push_back({splitPlaceholder.Start, uintptrDst - splitPlaceholder.Start});
            }
            if (uintptrDst+size < splitPlaceholder.Start+splitPlaceholder.Size)
            {
                //Log(LogLevel::Debug, "splitting on the right %llx\n", (splitPlaceholder.Start+splitPlaceholder.Size)-(uintptrDst+size));
                VirtmemPlaceholders.push_back({uintptrDst+size, (splitPlaceholder.Start+splitPlaceholder.Size)-(uintptrDst+size)});
            }

            if (!mapViewOfFile3Ptr(MemoryFile, nullptr, dst, offset, size, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0))
            {
                Log(LogLevel::Debug, "MapViewOfFile3 failed with %x\n", GetLastError());
                return false;
            }

            return true;
        }
    }

    Log(LogLevel::Debug, "no mapping at all found??? %p %x %p\n", dst, size, MemoryBase);
    return false;
#else
    return mmap(dst, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, MemoryFile, offset) != MAP_FAILED;
#endif
}

bool ARMJIT_Memory::UnmapFromRange(u32 addr, u32 num, u32 offset, u32 size) noexcept
{
    CHECK_ALIGNED(addr);
    CHECK_ALIGNED(offset);
    CHECK_ALIGNED(size);

    u8* dst = (u8*)(num == 0 ? FastMem9Start : FastMem7Start) + addr;
#ifdef __SWITCH__
    Result r = svcUnmapProcessMemory(dst, envGetOwnProcessHandle(),
        (u64)(MemoryBaseCodeMem + offset), size);
    return R_SUCCEEDED(r);
#elif defined(_WIN32)
    if (!UnmapViewOfFileEx(dst, MEM_PRESERVE_PLACEHOLDER))
    {
        Log(LogLevel::Debug, "UnmapViewOfFileEx failed %x\n", GetLastError());
        return false;
    }

    uintptr_t uintptrDst = reinterpret_cast<uintptr_t>(dst);
    uintptr_t coalesceStart = uintptrDst;
    size_t coalesceSize = size;

    for (auto it = VirtmemPlaceholders.begin(); it != VirtmemPlaceholders.end();)
    {
        if (it->Start+it->Size == uintptrDst)
        {
            //Log(LogLevel::Debug, "Coalescing to the left\n");
            coalesceStart = it->Start;
            coalesceSize += it->Size;
            it = VirtmemPlaceholders.erase(it);
        }
        else if (it->Start == uintptrDst+size)
        {
            //Log(LogLevel::Debug, "Coalescing to the right\n");
            coalesceSize += it->Size;
            it = VirtmemPlaceholders.erase(it);
        }
        else
        {
            it++;
        }
    }

    if (coalesceStart != uintptrDst || coalesceSize != size)
    {
        if (!VirtualFree(reinterpret_cast<void*>(coalesceStart), coalesceSize, MEM_RELEASE|MEM_COALESCE_PLACEHOLDERS))
            return false;

    }
    VirtmemPlaceholders.push_back({coalesceStart, coalesceSize});
    //Log(LogLevel::Debug, "Adding coalesced region %llx %llx", coalesceStart, coalesceSize);

    return true;
#else
    return mmap(dst, size, PROT_NONE, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0) != MAP_FAILED;
#endif
}

#ifndef __SWITCH__
void ARMJIT_Memory::SetCodeProtectionRange(u32 addr, u32 size, u32 num, int protection) noexcept
{
    CHECK_ALIGNED(addr);
    CHECK_ALIGNED(size);

    u8* dst = (u8*)(num == 0 ? FastMem9Start : FastMem7Start) + addr;
#if defined(_WIN32)
    DWORD winProtection, oldProtection;
    if (protection == 0)
        winProtection = PAGE_NOACCESS;
    else if (protection == 1)
        winProtection = PAGE_READONLY;
    else
        winProtection = PAGE_READWRITE;
    bool success = VirtualProtect(dst, size, winProtection, &oldProtection);
    if (!success)
    {
        Log(LogLevel::Debug, "VirtualProtect failed with %x\n", GetLastError());
    }
    assert(success);
#else
    int posixProt;
    if (protection == 0)
        posixProt = PROT_NONE;
    else if (protection == 1)
        posixProt = PROT_READ;
    else
        posixProt = PROT_READ | PROT_WRITE;
    mprotect(dst, size, posixProt);
#endif
}
#endif

void ARMJIT_Memory::Mapping::Unmap(int region, melonDS::NDS& nds) noexcept
{
    u32 dtcmStart = nds.ARM9.DTCMBase;
    u32 dtcmSize = ~nds.ARM9.DTCMMask + 1;
    bool skipDTCM = Num == 0 && region != memregion_DTCM;
    u8* statuses = Num == 0 ? nds.JIT.Memory.MappingStatus9 : nds.JIT.Memory.MappingStatus7;
    u32 offset = 0;
    while (offset < Size)
    {
        if (skipDTCM && Addr + offset == dtcmStart)
        {
            offset += dtcmSize;
        }
        else
        {
            u32 segmentOffset = offset;
            u8 status = statuses[(Addr + offset) >> PageShift];
            while (statuses[(Addr + offset) >> PageShift] == status
                   && offset < Size
                   && (!skipDTCM || Addr + offset != dtcmStart))
            {
                assert(statuses[(Addr + offset) >> PageShift] != memstate_Unmapped);
                statuses[(Addr + offset) >> PageShift] = memstate_Unmapped;
                offset += PageSize;
            }

#ifdef __SWITCH__
            if (status == memstate_MappedRW)
            {
                u32 segmentSize = offset - segmentOffset;
                Log(LogLevel::Debug, "unmapping %x %x %x %x\n", Addr + segmentOffset, Num, segmentOffset + LocalOffset + OffsetsPerRegion[region], segmentSize);
                bool success = memory.UnmapFromRange(Addr + segmentOffset, Num, segmentOffset + LocalOffset + OffsetsPerRegion[region], segmentSize);
                assert(success);
            }
#endif
        }
    }

#ifndef __SWITCH__
    u32 dtcmEnd = dtcmStart + dtcmSize;
    if (Num == 0
        && dtcmEnd >= Addr
        && dtcmStart < Addr + Size)
    {
        bool success;
        if (dtcmStart > Addr)
        {
            success = nds.JIT.Memory.UnmapFromRange(Addr, 0, OffsetsPerRegion[region] + LocalOffset, dtcmStart - Addr);
            assert(success);
        }
        if (dtcmEnd < Addr + Size)
        {
            u32 offset = dtcmStart - Addr + dtcmSize;
            success = nds.JIT.Memory.UnmapFromRange(dtcmEnd, 0, OffsetsPerRegion[region] + LocalOffset + offset, Size - offset);
            assert(success);
        }
    }
    else
    {
        bool succeded = nds.JIT.Memory.UnmapFromRange(Addr, Num, OffsetsPerRegion[region] + LocalOffset, Size);
        assert(succeded);
    }
#endif
}

void ARMJIT_Memory::SetCodeProtection(int region, u32 offset, bool protect) noexcept
{
    offset &= ~(PageSize - 1);
    //printf("set code protection %d %x %d\n", region, offset, protect);

    for (int i = 0; i < Mappings[region].Length; i++)
    {
        Mapping& mapping = Mappings[region][i];

        if (offset < mapping.LocalOffset || offset >= mapping.LocalOffset + mapping.Size)
            continue;

        u32 effectiveAddr = mapping.Addr + (offset - mapping.LocalOffset);
        if (mapping.Num == 0
            && region != memregion_DTCM
            && (effectiveAddr & NDS.ARM9.DTCMMask) == NDS.ARM9.DTCMBase)
            continue;

        u8* states = (u8*)(mapping.Num == 0 ? MappingStatus9 : MappingStatus7);

        //printf("%x %d %x %x %x %d\n", effectiveAddr, mapping.Num, mapping.Addr, mapping.LocalOffset, mapping.Size, states[effectiveAddr >> PageShift]);
        assert(states[effectiveAddr >> PageShift] == (protect ? memstate_MappedRW : memstate_MappedProtected));
        states[effectiveAddr >> PageShift] = protect ? memstate_MappedProtected : memstate_MappedRW;

#if defined(__SWITCH__)
        bool success;
        if (protect)
            success = UnmapFromRange(effectiveAddr, mapping.Num, OffsetsPerRegion[region] + offset, 0x1000);
        else
            success = MapIntoRange(effectiveAddr, mapping.Num, OffsetsPerRegion[region] + offset, 0x1000);
        assert(success);
#else
        SetCodeProtectionRange(effectiveAddr, PageSize, mapping.Num, protect ? 1 : 2);
#endif
    }
}

void ARMJIT_Memory::RemapDTCM(u32 newBase, u32 newSize) noexcept
{
    // this first part could be made more efficient
    // by unmapping DTCM first and then map the holes
    u32 oldDTCMBase = NDS.ARM9.DTCMBase;
    u32 oldDTCMSize = ~NDS.ARM9.DTCMMask + 1;
    u32 oldDTCMEnd = oldDTCMBase + NDS.ARM9.DTCMMask;

    u32 newEnd = newBase + newSize;

    Log(LogLevel::Debug, "remapping DTCM %x %x %x %x\n", newBase, newEnd, oldDTCMBase, oldDTCMEnd);
    // unmap all regions containing the old or the current DTCM mapping
    for (int region = 0; region < memregions_Count; region++)
    {
        if (region == memregion_DTCM)
            continue;

        for (int i = 0; i < Mappings[region].Length;)
        {
            Mapping& mapping = Mappings[region][i];

            u32 start = mapping.Addr;
            u32 end = mapping.Addr + mapping.Size;

            Log(LogLevel::Debug, "unmapping %d %x %x %x %x\n", region, mapping.Addr, mapping.Size, mapping.Num, mapping.LocalOffset);

            bool overlap = (oldDTCMSize > 0 && oldDTCMBase < end && oldDTCMEnd > start)
                || (newSize > 0 && newBase < end && newEnd > start);

            if (mapping.Num == 0 && overlap)
            {
                mapping.Unmap(region, NDS);
                Mappings[region].Remove(i);
            }
            else
            {
                i++;
            }
        }
    }

    for (int i = 0; i < Mappings[memregion_DTCM].Length; i++)
    {
        Mappings[memregion_DTCM][i].Unmap(memregion_DTCM, NDS);
    }
    Mappings[memregion_DTCM].Clear();
}

void ARMJIT_Memory::RemapNWRAM(int num) noexcept
{
    if (NDS.ConsoleType == 0)
        return;

    auto* dsi = static_cast<DSi*>(&NDS);
    for (int i = 0; i < Mappings[memregion_SharedWRAM].Length;)
    {
        Mapping& mapping = Mappings[memregion_SharedWRAM][i];
        if (dsi->NWRAMStart[mapping.Num][num] < mapping.Addr + mapping.Size
            && dsi->NWRAMEnd[mapping.Num][num] > mapping.Addr)
        {
            mapping.Unmap(memregion_SharedWRAM, NDS);
            Mappings[memregion_SharedWRAM].Remove(i);
        }
        else
        {
            i++;
        }
    }
    for (int i = 0; i < Mappings[memregion_NewSharedWRAM_A + num].Length; i++)
    {
        Mappings[memregion_NewSharedWRAM_A + num][i].Unmap(memregion_NewSharedWRAM_A + num, NDS);
    }
    Mappings[memregion_NewSharedWRAM_A + num].Clear();
}

void ARMJIT_Memory::RemapSWRAM() noexcept
{
    Log(LogLevel::Debug, "remapping SWRAM\n");
    for (int i = 0; i < Mappings[memregion_WRAM7].Length;)
    {
        Mapping& mapping = Mappings[memregion_WRAM7][i];
        if (mapping.Addr + mapping.Size <= 0x03800000)
        {
            mapping.Unmap(memregion_WRAM7, NDS);
            Mappings[memregion_WRAM7].Remove(i);
        }
        else
            i++;
    }
    for (int i = 0; i < Mappings[memregion_SharedWRAM].Length; i++)
    {
        Mappings[memregion_SharedWRAM][i].Unmap(memregion_SharedWRAM, NDS);
    }
    Mappings[memregion_SharedWRAM].Clear();
}

bool ARMJIT_Memory::MapAtAddress(u32 addr) noexcept
{
    u32 num = NDS.CurCPU;

    int region = num == 0
        ? ClassifyAddress9(addr)
        : ClassifyAddress7(addr);

    if (!IsFastmemCompatible(region))
        return false;

    u32 mirrorStart, mirrorSize, memoryOffset;
    bool isMapped = GetMirrorLocation(region, num, addr, memoryOffset, mirrorStart, mirrorSize);
    if (!isMapped)
        return false;

    u8* states = num == 0 ? MappingStatus9 : MappingStatus7;
    //printf("mapping mirror %x, %x %x %d %d\n", mirrorStart, mirrorSize, memoryOffset, region, num);
    bool isExecutable = NDS.JIT.CodeMemRegions[region];

    u32 dtcmStart = NDS.ARM9.DTCMBase;
    u32 dtcmSize = ~NDS.ARM9.DTCMMask + 1;
    u32 dtcmEnd = dtcmStart + dtcmSize;
#ifndef __SWITCH__
    if (num == 0
        && dtcmEnd >= mirrorStart
        && dtcmStart < mirrorStart + mirrorSize)
    {
        if (dtcmSize < PageSize)
        {
            // we could technically mask out the DTCM by setting a hole to access permissions
            // but realistically there isn't much of a point in mapping less than 16kb of DTCM
            // so it isn't worth more complex support
            Log(LogLevel::Info, "DTCM size smaller than 16kb skipping mapping entirely");
            return false;
        }

        bool success;
        if (dtcmStart > mirrorStart)
        {
            success = MapIntoRange(mirrorStart, 0, OffsetsPerRegion[region] + memoryOffset, dtcmStart - mirrorStart);
            assert(success);
        }
        if (dtcmEnd < mirrorStart + mirrorSize)
        {
            u32 offset = dtcmStart - mirrorStart + dtcmSize;
            success = MapIntoRange(dtcmEnd, 0, OffsetsPerRegion[region] + memoryOffset + offset, mirrorSize - offset);
            assert(success);
        }
    }
    else
    {
        bool succeded = MapIntoRange(mirrorStart, num, OffsetsPerRegion[region] + memoryOffset, mirrorSize);
        assert(succeded);
    }
#endif

    AddressRange* range = NDS.JIT.CodeMemRegions[region] + memoryOffset / 512;

    // this overcomplicated piece of code basically just finds whole pieces of code memory
    // which can be mapped/protected
    u32 offset = 0;
    bool skipDTCM = num == 0 && region != memregion_DTCM;
    while (offset < mirrorSize)
    {
        if (skipDTCM && mirrorStart + offset == dtcmStart)
        {
            offset += dtcmSize;
        }
        else
        {
            u32 sectionOffset = offset;
            bool hasCode = isExecutable && PageContainsCode(&range[offset / 512], PageSize);
            while (offset < mirrorSize
                && (!isExecutable || PageContainsCode(&range[offset / 512], PageSize) == hasCode)
                && (!skipDTCM || mirrorStart + offset != NDS.ARM9.DTCMBase))
            {
                assert(states[(mirrorStart + offset) >> PageShift] == memstate_Unmapped);
                states[(mirrorStart + offset) >> PageShift] = hasCode ? memstate_MappedProtected : memstate_MappedRW;
                offset += PageSize;
            }

            u32 sectionSize = offset - sectionOffset;

#if defined(__SWITCH__)
            if (!hasCode)
            {
                //printf("trying to map %x (size: %x) from %x\n", mirrorStart + sectionOffset, sectionSize, sectionOffset + memoryOffset + OffsetsPerRegion[region]);
                bool succeded = MapIntoRange(mirrorStart + sectionOffset, num, sectionOffset + memoryOffset + OffsetsPerRegion[region], sectionSize);
                assert(succeded);
            }
#else
            if (hasCode)
            {
                SetCodeProtectionRange(mirrorStart + sectionOffset, sectionSize, num, 1);
            }
#endif
        }
    }

    assert(num == 0 || num == 1);
    Mapping mapping{mirrorStart, mirrorSize, memoryOffset, num};
    Mappings[region].Add(mapping);

    //printf("mapped mirror at %08x-%08x\n", mirrorStart, mirrorStart + mirrorSize - 1);

    return true;
}

u32 ARMJIT_Memory::PageSize = 0;
u32 ARMJIT_Memory::PageShift = 0;

bool ARMJIT_Memory::IsFastMemSupported()
{
#ifdef __APPLE__
    return false;
#else
    static bool initialised = false;
    static bool isSupported = false;
    if (!initialised)
    {
#ifdef _WIN32
        ARMJIT_Global::Init();
        isSupported = virtualAlloc2Ptr != nullptr;
        ARMJIT_Global::DeInit();

        PageSize = RegularPageSize;
#else
        PageSize = sysconf(_SC_PAGESIZE);
        isSupported = PageSize == RegularPageSize || PageSize == LargePageSize;
#endif
        PageShift = __builtin_ctz(PageSize);
        initialised = true;
    }
    return isSupported;
#endif
}

void ARMJIT_Memory::RegisterFaultHandler()
{
#ifdef _WIN32
    ExceptionHandlerHandle = AddVectoredExceptionHandler(1, ExceptionHandler);

    KernelBaseDll = LoadLibrary("KernelBase.dll");
   if (KernelBaseDll)
    {
        virtualAlloc2Ptr = reinterpret_cast<VirtualAlloc2Type>(GetProcAddress(KernelBaseDll, "VirtualAlloc2"));
        mapViewOfFile3Ptr = reinterpret_cast<MapViewOfFile3Type>(GetProcAddress(KernelBaseDll, "MapViewOfFile3"));
    }

    if (!virtualAlloc2Ptr)
    {
        Log(LogLevel::Error, "Could not load new Windows virtual memory functions, fast memory is disabled.\n");
    }
#else
    struct sigaction sa;
    sa.sa_handler = nullptr;
    sa.sa_sigaction = &SigsegvHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &OldSaSegv);
#ifdef __APPLE__
    sigaction(SIGBUS, &sa, &OldSaBus);
#endif
#endif
}

void ARMJIT_Memory::UnregisterFaultHandler()
{
#ifdef _WIN32
    if (ExceptionHandlerHandle)
    {
        RemoveVectoredExceptionHandler(ExceptionHandlerHandle);
        ExceptionHandlerHandle = nullptr;
    }

    if (KernelBaseDll)
    {
        FreeLibrary(KernelBaseDll);
        KernelBaseDll = nullptr;
    }
#else
    sigaction(SIGSEGV, &OldSaSegv, nullptr);
#ifdef __APPLE__
    sigaction(SIGBUS, &OldSaBus, nullptr);
#endif
#endif
}

bool ARMJIT_Memory::FaultHandler(FaultDescription& faultDesc, melonDS::NDS& nds)
{
    if (nds.JIT.JITCompiler.IsJITFault(faultDesc.FaultPC))
    {
        bool rewriteToSlowPath = true;

        u8* memStatus = nds.CurCPU == 0 ? nds.JIT.Memory.MappingStatus9 : nds.JIT.Memory.MappingStatus7;

        if (memStatus[faultDesc.EmulatedFaultAddr >> PageShift] == memstate_Unmapped)
            rewriteToSlowPath = !nds.JIT.Memory.MapAtAddress(faultDesc.EmulatedFaultAddr);

        if (rewriteToSlowPath)
            faultDesc.FaultPC = nds.JIT.JITCompiler.RewriteMemAccess(faultDesc.FaultPC);

        return true;
    }
    return false;
}

ARMJIT_Memory::ARMJIT_Memory(melonDS::NDS& nds) : NDS(nds)
{
    ARMJIT_Global::Init();
#if defined(__SWITCH__)
    MemoryBase = (u8*)aligned_alloc(0x1000, MemoryTotalSize);
    virtmemLock();
    MemoryBaseCodeMem = (u8*)virtmemFindCodeMemory(MemoryTotalSize, 0x1000);

    bool succeded = R_SUCCEEDED(svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)MemoryBaseCodeMem,
        (u64)MemoryBase, MemoryTotalSize));
    assert(succeded);
    succeded = R_SUCCEEDED(svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64)MemoryBaseCodeMem,
        MemoryTotalSize, Perm_Rw));
    assert(succeded);

    // 8 GB of address space, just don't ask...
    FastMem9Start = virtmemFindAslr(AddrSpaceSize, 0x1000);
    assert(FastMem9Start);
    FastMem7Start = virtmemFindAslr(AddrSpaceSize, 0x1000);
    assert(FastMem7Start);

    FastMem9Reservation = virtmemAddReservation(FastMem9Start, AddrSpaceSize);
    FastMem7Reservation = virtmemAddReservation(FastMem7Start, AddrSpaceSize);
    virtmemUnlock();

    u8* basePtr = MemoryBaseCodeMem;
#elif defined(_WIN32)
    if (virtualAlloc2Ptr)
    {
        MemoryFile = CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, MemoryTotalSize, nullptr);

        MemoryBase = reinterpret_cast<u8*>(virtualAlloc2Ptr(nullptr, nullptr, VirtmemAreaSize,
            MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
            PAGE_NOACCESS,
            nullptr, 0));
        // split off placeholder and map base mapping
        VirtualFree(MemoryBase, MemoryTotalSize, MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER);
        mapViewOfFile3Ptr(MemoryFile, nullptr, MemoryBase, 0, MemoryTotalSize, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);

        VirtmemPlaceholders.push_back({reinterpret_cast<uintptr_t>(MemoryBase)+MemoryTotalSize, AddrSpaceSize*2});
    }
    else
    {
        // old Windows version
        MemoryBase = new u8[MemoryTotalSize];
    }
#else
    MemoryBase = (u8*)mmap(nullptr, VirtmemAreaSize, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);

#if defined(__ANDROID__)
    Libandroid = Platform::DynamicLibrary_Load("libandroid.so");
    using type_ASharedMemory_create = int(*)(const char* name, size_t size);
    auto ASharedMemory_create = reinterpret_cast<type_ASharedMemory_create>(Platform::DynamicLibrary_LoadFunction(Libandroid, "ASharedMemory_create"));

    if (ASharedMemory_create)
    {
        MemoryFile = ASharedMemory_create("melondsfastmem", MemoryTotalSize);
    }
    else
    {
        int fd = open(ASHMEM_DEVICE, O_RDWR);
        ioctl(fd, ASHMEM_SET_NAME, "melondsfastmem");
        ioctl(fd, ASHMEM_SET_SIZE, MemoryTotalSize);
        MemoryFile = fd;
    }
#else
    char fastmemPidName[snprintf(NULL, 0, "/melondsfastmem%d", getpid()) + 1];
    snprintf(fastmemPidName, sizeof(fastmemPidName), "/melondsfastmem%d", getpid());
    MemoryFile = shm_open(fastmemPidName, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (MemoryFile == -1)
    {
        Log(LogLevel::Error, "Failed to open memory using shm_open! (%s)", strerror(errno));
    }
    shm_unlink(fastmemPidName);
#endif
    if (ftruncate(MemoryFile, MemoryTotalSize) < 0)
    {
        Log(LogLevel::Error, "Failed to allocate memory using ftruncate! (%s)", strerror(errno));
    }

    mmap(MemoryBase, MemoryTotalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, MemoryFile, 0);
#endif
    FastMem9Start = MemoryBase+MemoryTotalSize;
    FastMem7Start = static_cast<u8*>(FastMem9Start)+AddrSpaceSize;
}

ARMJIT_Memory::~ARMJIT_Memory() noexcept
{
#if defined(__SWITCH__)
    virtmemLock();
    if (FastMem9Reservation)
        virtmemRemoveReservation(FastMem9Reservation);

    if (FastMem7Reservation)
        virtmemRemoveReservation(FastMem7Reservation);

    FastMem9Reservation = nullptr;
    FastMem7Reservation = nullptr;
    virtmemUnlock();

    svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)MemoryBaseCodeMem, (u64)MemoryBase, MemoryTotalSize);
    free(MemoryBase);
    MemoryBase = nullptr;
#elif defined(_WIN32)
    if (virtualAlloc2Ptr)
    {
        if (MemoryBase)
        {
            bool viewUnmapped = UnmapViewOfFileEx(MemoryBase, MEM_PRESERVE_PLACEHOLDER);
            assert(viewUnmapped);
            bool viewCoalesced = VirtualFree(MemoryBase, VirtmemAreaSize, MEM_RELEASE|MEM_COALESCE_PLACEHOLDERS);
            assert(viewCoalesced);
            bool freeEverything = VirtualFree(MemoryBase, 0, MEM_RELEASE);
            assert(freeEverything);

            MemoryBase = nullptr;
            FastMem9Start = nullptr;
            FastMem7Start = nullptr;
        }

        if (MemoryFile)
        {
            CloseHandle(MemoryFile);
            MemoryFile = INVALID_HANDLE_VALUE;
        }
    }
    else
    {
        delete[] MemoryBase;
    }
#else
    if (MemoryBase)
    {
        munmap(MemoryBase, VirtmemAreaSize);
        MemoryBase = nullptr;
        FastMem9Start = nullptr;
        FastMem7Start = nullptr;
    }

    if (MemoryFile >= 0)
    {
        close(MemoryFile);
        MemoryFile = -1;
    }

    Log(LogLevel::Info, "unmappinged everything\n");

#if defined(__ANDROID__)
    if (Libandroid)
    {
        Platform::DynamicLibrary_Unload(Libandroid);
        Libandroid = nullptr;
    }
#endif

#endif

    ARMJIT_Global::DeInit();
}

void ARMJIT_Memory::Reset() noexcept
{
    for (int region = 0; region < memregions_Count; region++)
    {
        for (int i = 0; i < Mappings[region].Length; i++)
            Mappings[region][i].Unmap(region, NDS);
        Mappings[region].Clear();
    }

    for (size_t i = 0; i < sizeof(MappingStatus9); i++)
    {
        assert(MappingStatus9[i] == memstate_Unmapped);
        assert(MappingStatus7[i] == memstate_Unmapped);
    }

    Log(LogLevel::Debug, "done resetting jit mem\n");
}

bool ARMJIT_Memory::IsFastmemCompatible(int region) const noexcept
{
    return OffsetsPerRegion[region] != UINT32_MAX;
}

bool ARMJIT_Memory::GetMirrorLocation(int region, u32 num, u32 addr, u32& memoryOffset, u32& mirrorStart, u32& mirrorSize) const noexcept
{
    memoryOffset = 0;
    switch (region)
    {
    case memregion_ITCM:
        if (num == 0)
        {
            mirrorStart = addr & ~(ITCMPhysicalSize - 1);
            mirrorSize = ITCMPhysicalSize;
            return true;
        }
        return false;
    case memregion_DTCM:
        if (num == 0)
        {
            mirrorStart = addr & ~(DTCMPhysicalSize - 1);
            mirrorSize = DTCMPhysicalSize;
            return true;
        }
        return false;
    case memregion_MainRAM:
        mirrorStart = addr & ~NDS.MainRAMMask;
        mirrorSize = NDS.MainRAMMask + 1;
        return true;
    case memregion_BIOS9:
        if (num == 0)
        {
            mirrorStart = addr & ~0xFFF;
            mirrorSize = 0x1000;
            return true;
        }
        return false;
    case memregion_BIOS7:
        if (num == 1)
        {
            mirrorStart = 0;
            mirrorSize = 0x4000;
            return true;
        }
        return false;
    case memregion_SharedWRAM:
        if (num == 0 && NDS.SWRAM_ARM9.Mem)
        {
            mirrorStart = addr & ~NDS.SWRAM_ARM9.Mask;
            mirrorSize = NDS.SWRAM_ARM9.Mask + 1;
            memoryOffset = NDS.SWRAM_ARM9.Mem - GetSharedWRAM();
            return true;
        }
        else if (num == 1 && NDS.SWRAM_ARM7.Mem)
        {
            mirrorStart = addr & ~NDS.SWRAM_ARM7.Mask;
            mirrorSize = NDS.SWRAM_ARM7.Mask + 1;
            memoryOffset = NDS.SWRAM_ARM7.Mem - GetSharedWRAM();
            return true;
        }
        return false;
    case memregion_WRAM7:
        if (num == 1)
        {
            mirrorStart = addr & ~(ARM7WRAMSize - 1);
            mirrorSize = ARM7WRAMSize;
            return true;
        }
        return false;
    case memregion_VRAM:
        if (num == 0)
        {
            mirrorStart = addr & ~0xFFFFF;
            mirrorSize = 0x100000;
            return true;
        }
        return false;
    case memregion_VWRAM:
        if (num == 1)
        {
            mirrorStart = addr & ~0x3FFFF;
            mirrorSize = 0x40000;
            return true;
        }
        return false;
    case memregion_NewSharedWRAM_A:
        {
            auto* dsi = dynamic_cast<DSi*>(&NDS);
            assert(dsi != nullptr);
            u8* ptr = dsi->NWRAMMap_A[num][(addr >> 16) & dsi->NWRAMMask[num][0]];
            if (ptr)
            {
                memoryOffset = ptr - GetNWRAM_A();
                mirrorStart = addr & ~0xFFFF;
                mirrorSize = 0x10000;
                return true;
            }
            return false; // zero filled memory
        }
    case memregion_NewSharedWRAM_B:
        {
            auto* dsi = dynamic_cast<DSi*>(&NDS);
            assert(dsi != nullptr);
            u8* ptr = dsi->NWRAMMap_B[num][(addr >> 15) & dsi->NWRAMMask[num][1]];
            if (ptr)
            {
                memoryOffset = ptr - GetNWRAM_B();
                mirrorStart = addr & ~0x7FFF;
                mirrorSize = 0x8000;
                return true;
            }
            return false; // zero filled memory
        }
    case memregion_NewSharedWRAM_C:
        {
            auto* dsi = dynamic_cast<DSi*>(&NDS);
            assert(dsi != nullptr);
            u8* ptr = dsi->NWRAMMap_C[num][(addr >> 15) & dsi->NWRAMMask[num][2]];
            if (ptr)
            {
                memoryOffset = ptr - GetNWRAM_C();
                mirrorStart = addr & ~0x7FFF;
                mirrorSize = 0x8000;
                return true;
            }
            return false; // zero filled memory
        }
    case memregion_BIOS9DSi:
        if (num == 0)
        {
            auto* dsi = dynamic_cast<DSi*>(&NDS);
            assert(dsi != nullptr);
            mirrorStart = addr & ~0xFFFF;
            mirrorSize = dsi->SCFG_BIOS & (1<<0) ? 0x8000 : 0x10000;
            return true;
        }
        return false;
    case memregion_BIOS7DSi:
        if (num == 1)
        {
            auto* dsi = dynamic_cast<DSi*>(&NDS);
            assert(dsi != nullptr);
            mirrorStart = addr & ~0xFFFF;
            mirrorSize = dsi->SCFG_BIOS & (1<<8) ? 0x8000 : 0x10000;
            return true;
        }
        return false;
    default:
        assert(false && "For the time being this should only be used for code");
        return false;
    }
}

u32 ARMJIT_Memory::LocaliseAddress(int region, u32 num, u32 addr) const noexcept
{
    switch (region)
    {
    case memregion_ITCM:
        return (addr & (ITCMPhysicalSize - 1)) | (memregion_ITCM << 27);
    case memregion_MainRAM:
        return (addr & NDS.MainRAMMask) | (memregion_MainRAM << 27);
    case memregion_BIOS9:
        return (addr & 0xFFF) | (memregion_BIOS9 << 27);
    case memregion_BIOS7:
        return (addr & 0x3FFF) | (memregion_BIOS7 << 27);
    case memregion_SharedWRAM:
        if (num == 0)
            return ((addr & NDS.SWRAM_ARM9.Mask) + (NDS.SWRAM_ARM9.Mem - GetSharedWRAM())) | (memregion_SharedWRAM << 27);
        else
            return ((addr & NDS.SWRAM_ARM7.Mask) + (NDS.SWRAM_ARM7.Mem - GetSharedWRAM())) | (memregion_SharedWRAM << 27);
    case memregion_WRAM7:
        return (addr & (melonDS::ARM7WRAMSize - 1)) | (memregion_WRAM7 << 27);
    case memregion_VRAM:
        // TODO: take mapping properly into account
        return (addr & 0xFFFFF) | (memregion_VRAM << 27);
    case memregion_VWRAM:
        // same here
        return (addr & 0x3FFFF) | (memregion_VWRAM << 27);
    case memregion_NewSharedWRAM_A:
        {
            auto* dsi = dynamic_cast<DSi*>(&NDS);
            assert(dsi != nullptr);
            u8* ptr = dsi->NWRAMMap_A[num][(addr >> 16) & dsi->NWRAMMask[num][0]];
            if (ptr)
                return (ptr - GetNWRAM_A() + (addr & 0xFFFF)) | (memregion_NewSharedWRAM_A << 27);
            else
                return memregion_Other << 27; // zero filled memory
        }
    case memregion_NewSharedWRAM_B:
        {
            auto* dsi = dynamic_cast<DSi*>(&NDS);
            assert(dsi != nullptr);
            u8* ptr = dsi->NWRAMMap_B[num][(addr >> 15) & dsi->NWRAMMask[num][1]];
            if (ptr)
                return (ptr - GetNWRAM_B() + (addr & 0x7FFF)) | (memregion_NewSharedWRAM_B << 27);
            else
                return memregion_Other << 27;
        }
    case memregion_NewSharedWRAM_C:
        {
            auto* dsi = dynamic_cast<DSi*>(&NDS);
            assert(dsi != nullptr);
            u8* ptr = dsi->NWRAMMap_C[num][(addr >> 15) & dsi->NWRAMMask[num][2]];
            if (ptr)
                return (ptr - GetNWRAM_C() + (addr & 0x7FFF)) | (memregion_NewSharedWRAM_C << 27);
            else
                return memregion_Other << 27;
        }
    case memregion_BIOS9DSi:
    case memregion_BIOS7DSi:
        return (addr & 0xFFFF) | (region << 27);
    default:
        assert(false && "This should only be needed for regions which can contain code");
        return memregion_Other << 27;
    }
}

int ARMJIT_Memory::ClassifyAddress9(u32 addr) const noexcept
{
    if (addr < NDS.ARM9.ITCMSize)
    {
        return memregion_ITCM;
    }
    else if ((addr & NDS.ARM9.DTCMMask) == NDS.ARM9.DTCMBase)
    {
        return memregion_DTCM;
    }
    else
    {
        if (NDS.ConsoleType == 1)
        {
            auto& dsi = static_cast<DSi&>(NDS);
            if (addr >= 0xFFFF0000 && !(dsi.SCFG_BIOS & (1<<1)))
            {
                if ((addr >= 0xFFFF8000) && (dsi.SCFG_BIOS & (1<<0)))
                    return memregion_Other;

                return memregion_BIOS9DSi;
            }
        }

        if ((addr & 0xFFFFF000) == 0xFFFF0000)
        {
            return memregion_BIOS9;
        }

        switch (addr & 0xFF000000)
        {
        case 0x02000000:
            return memregion_MainRAM;
        case 0x03000000:
            if (NDS.ConsoleType == 1)
            {
                auto& dsi = static_cast<DSi&>(NDS);
                if (addr >= dsi.NWRAMStart[0][0] && addr < dsi.NWRAMEnd[0][0])
                    return memregion_NewSharedWRAM_A;
                if (addr >= dsi.NWRAMStart[0][1] && addr < dsi.NWRAMEnd[0][1])
                    return memregion_NewSharedWRAM_B;
                if (addr >= dsi.NWRAMStart[0][2] && addr < dsi.NWRAMEnd[0][2])
                    return memregion_NewSharedWRAM_C;
            }

            if (NDS.SWRAM_ARM9.Mem)
                return memregion_SharedWRAM;
            return memregion_Other;
        case 0x04000000:
            return memregion_IO9;
        case 0x06000000:
            return memregion_VRAM;
        case 0x0C000000:
            return (NDS.ConsoleType==1) ? memregion_MainRAM : memregion_Other;
        default:
            return memregion_Other;
        }
    }
}

int ARMJIT_Memory::ClassifyAddress7(u32 addr) const noexcept
{
    if (NDS.ConsoleType == 1)
    {
        auto& dsi = static_cast<DSi&>(NDS);
        if (addr < 0x00010000 && !(dsi.SCFG_BIOS & (1<<9)))
        {
            if (addr >= 0x00008000 && dsi.SCFG_BIOS & (1<<8))
                return memregion_Other;

            return memregion_BIOS7DSi;
        }
    }

    if (addr < 0x00004000)
    {
        return memregion_BIOS7;
    }
    else
    {
        switch (addr & 0xFF800000)
        {
        case 0x02000000:
        case 0x02800000:
            return memregion_MainRAM;
        case 0x03000000:
            if (NDS.ConsoleType == 1)
            {
                auto& dsi = static_cast<DSi&>(NDS);
                if (addr >= dsi.NWRAMStart[1][0] && addr < dsi.NWRAMEnd[1][0])
                    return memregion_NewSharedWRAM_A;
                if (addr >= dsi.NWRAMStart[1][1] && addr < dsi.NWRAMEnd[1][1])
                    return memregion_NewSharedWRAM_B;
                if (addr >= dsi.NWRAMStart[1][2] && addr < dsi.NWRAMEnd[1][2])
                    return memregion_NewSharedWRAM_C;
            }

            if (NDS.SWRAM_ARM7.Mem)
                return memregion_SharedWRAM;
            return memregion_WRAM7;
        case 0x03800000:
            return memregion_WRAM7;
        case 0x04000000:
            return memregion_IO7;
        case 0x04800000:
            return memregion_Wifi;
        case 0x06000000:
        case 0x06800000:
            return memregion_VWRAM;
        case 0x0C000000:
        case 0x0C800000:
            return (NDS.ConsoleType==1) ? memregion_MainRAM : memregion_Other;
        default:
            return memregion_Other;
        }
    }
}

/*void WifiWrite32(u32 addr, u32 val)
{
    Wifi::Write(addr, val & 0xFFFF);
    Wifi::Write(addr + 2, val >> 16);
}

u32 WifiRead32(u32 addr)
{
    return (u32)Wifi::Read(addr) | ((u32)Wifi::Read(addr + 2) << 16);
}*/

template <typename T>
void VRAMWrite(u32 addr, T val)
{
    switch (addr & 0x00E00000)
    {
    case 0x00000000: NDS::Current->GPU.WriteVRAM_ABG<T>(addr, val); return;
    case 0x00200000: NDS::Current->GPU.WriteVRAM_BBG<T>(addr, val); return;
    case 0x00400000: NDS::Current->GPU.WriteVRAM_AOBJ<T>(addr, val); return;
    case 0x00600000: NDS::Current->GPU.WriteVRAM_BOBJ<T>(addr, val); return;
    default: NDS::Current->GPU.WriteVRAM_LCDC<T>(addr, val); return;
    }
}
template <typename T>
T VRAMRead(u32 addr)
{
    switch (addr & 0x00E00000)
    {
    case 0x00000000: return NDS::Current->GPU.ReadVRAM_ABG<T>(addr);
    case 0x00200000: return NDS::Current->GPU.ReadVRAM_BBG<T>(addr);
    case 0x00400000: return NDS::Current->GPU.ReadVRAM_AOBJ<T>(addr);
    case 0x00600000: return NDS::Current->GPU.ReadVRAM_BOBJ<T>(addr);
    default: return NDS::Current->GPU.ReadVRAM_LCDC<T>(addr);
    }
}

static u8 GPU3D_Read8(u32 addr) noexcept
{
    return NDS::Current->GPU.GPU3D.Read8(addr);
}

static u16 GPU3D_Read16(u32 addr) noexcept
{
    return NDS::Current->GPU.GPU3D.Read16(addr);
}

static u32 GPU3D_Read32(u32 addr) noexcept
{
    return NDS::Current->GPU.GPU3D.Read32(addr);
}

static void GPU3D_Write8(u32 addr, u8 val) noexcept
{
    NDS::Current->GPU.GPU3D.Write8(addr, val);
}

static void GPU3D_Write16(u32 addr, u16 val) noexcept
{
    NDS::Current->GPU.GPU3D.Write16(addr, val);
}

static void GPU3D_Write32(u32 addr, u32 val) noexcept
{
    NDS::Current->GPU.GPU3D.Write32(addr, val);
}

template<class T>
static T GPU_ReadVRAM_ARM7(u32 addr) noexcept
{
    return NDS::Current->GPU.ReadVRAM_ARM7<T>(addr);
}

template<class T>
static void GPU_WriteVRAM_ARM7(u32 addr, T val) noexcept
{
    NDS::Current->GPU.WriteVRAM_ARM7<T>(addr, val);
}

u32 NDSCartSlot_ReadROMData()
{ // TODO: Add a NDS* parameter, when NDS* is eventually implemented
    return NDS::Current->NDSCartSlot.ReadROMData();
}

static u8 NDS_ARM9IORead8(u32 addr)
{
    return NDS::Current->ARM9IORead8(addr);
}

static u16 NDS_ARM9IORead16(u32 addr)
{
    return NDS::Current->ARM9IORead16(addr);
}

static u32 NDS_ARM9IORead32(u32 addr)
{
    return NDS::Current->ARM9IORead32(addr);
}

static void NDS_ARM9IOWrite8(u32 addr, u8 val)
{
    NDS::Current->ARM9IOWrite8(addr, val);
}

static void NDS_ARM9IOWrite16(u32 addr, u16 val)
{
    NDS::Current->ARM9IOWrite16(addr, val);
}

static void NDS_ARM9IOWrite32(u32 addr, u32 val)
{
    NDS::Current->ARM9IOWrite32(addr, val);
}

static u8 NDS_ARM7IORead8(u32 addr)
{
    return NDS::Current->ARM7IORead8(addr);
}

static u16 NDS_ARM7IORead16(u32 addr)
{
    return NDS::Current->ARM7IORead16(addr);
}

static u32 NDS_ARM7IORead32(u32 addr)
{
    return NDS::Current->ARM7IORead32(addr);
}

static void NDS_ARM7IOWrite8(u32 addr, u8 val)
{
    NDS::Current->ARM7IOWrite8(addr, val);
}

static void NDS_ARM7IOWrite16(u32 addr, u16 val)
{
    NDS::Current->ARM7IOWrite16(addr, val);
}

static void NDS_ARM7IOWrite32(u32 addr, u32 val)
{
    NDS::Current->ARM7IOWrite32(addr, val);
}

void* ARMJIT_Memory::GetFuncForAddr(ARM* cpu, u32 addr, bool store, int size) const noexcept
{
    if (cpu->Num == 0)
    {
        switch (addr & 0xFF000000)
        {
        case 0x04000000:
            if (!store && size == 32 && addr == 0x04100010 && NDS.ExMemCnt[0] & (1<<11))
                return (void*)NDSCartSlot_ReadROMData;

            /*
                unfortunately we can't map GPU2D this way
                since it's hidden inside an object

                though GPU3D registers are accessed much more intensive
            */
            if (addr >= 0x04000320 && addr < 0x040006A4)
            {
                switch (size | store)
                {
                case 8: return (void*)GPU3D_Read8;
                case 9: return (void*)GPU3D_Write8;
                case 16: return (void*)GPU3D_Read16;
                case 17: return (void*)GPU3D_Write16;
                case 32: return (void*)GPU3D_Read32;
                case 33: return (void*)GPU3D_Write32;
                }
            }

            switch (size | store)
            {
            case 8: return (void*)NDS_ARM9IORead8;
            case 9: return (void*)NDS_ARM9IOWrite8;
            case 16: return (void*)NDS_ARM9IORead16;
            case 17: return (void*)NDS_ARM9IOWrite16;
            case 32: return (void*)NDS_ARM9IORead32;
            case 33: return (void*)NDS_ARM9IOWrite32;
            }
            // NDS::Current will delegate to the DSi versions of these methods
            // if it's really a DSi
            break;
        case 0x06000000:
            switch (size | store)
            {
            case 8: return (void*)VRAMRead<u8>;
            case 9: return NULL;
            case 16: return (void*)VRAMRead<u16>;
            case 17: return (void*)VRAMWrite<u16>;
            case 32: return (void*)VRAMRead<u32>;
            case 33: return (void*)VRAMWrite<u32>;
            }
            break;
        }
    }
    else
    {
        switch (addr & 0xFF800000)
        {
        case 0x04000000:
            /*if (addr >= 0x04000400 && addr < 0x04000520)
            {
                switch (size | store)
                {
                case 8: return (void*)SPU::Read8;
                case 9: return (void*)SPU::Write8;
                case 16: return (void*)SPU::Read16;
                case 17: return (void*)SPU::Write16;
                case 32: return (void*)SPU::Read32;
                case 33: return (void*)SPU::Write32;
                }
            }*/

            switch (size | store)
            {
            case 8: return (void*)NDS_ARM7IORead8;
            case 9: return (void*)NDS_ARM7IOWrite8;
            case 16: return (void*)NDS_ARM7IORead16;
            case 17: return (void*)NDS_ARM7IOWrite16;
            case 32: return (void*)NDS_ARM7IORead32;
            case 33: return (void*)NDS_ARM7IOWrite32;
            }
            break;
            // TODO: the wifi funcs also ought to check POWCNT
        /*case 0x04800000:
            if (addr < 0x04810000 && size >= 16)
            {
                switch (size | store)
                {
                case 16: return (void*)Wifi::Read;
                case 17: return (void*)Wifi::Write;
                case 32: return (void*)WifiRead32;
                case 33: return (void*)WifiWrite32;
                }
            }
            break;*/
        case 0x06000000:
        case 0x06800000:
            switch (size | store)
            {
            case 8: return (void*)GPU_ReadVRAM_ARM7<u8>;
            case 9: return (void*)GPU_WriteVRAM_ARM7<u8>;
            case 16: return (void*)GPU_ReadVRAM_ARM7<u16>;
            case 17: return (void*)GPU_WriteVRAM_ARM7<u16>;
            case 32: return (void*)GPU_ReadVRAM_ARM7<u32>;
            case 33: return (void*)GPU_WriteVRAM_ARM7<u32>;
            }
        }
    }
    return NULL;
}
}
