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

#include <stdio.h>
#include <string.h>
#include "NDS.h"
#include "DSi.h"
#include "AREngine.h"
#include "Platform.h"

namespace melonDS
{

using Platform::Log;
using Platform::LogLevel;

AREngine::AREngine(melonDS::NDS& nds) : NDS(nds)
{
}

#define case16(x) \
    case ((x)+0x00): case ((x)+0x01): case ((x)+0x02): case ((x)+0x03): \
    case ((x)+0x04): case ((x)+0x05): case ((x)+0x06): case ((x)+0x07): \
    case ((x)+0x08): case ((x)+0x09): case ((x)+0x0A): case ((x)+0x0B): \
    case ((x)+0x0C): case ((x)+0x0D): case ((x)+0x0E): case ((x)+0x0F)

void AREngine::RunCheat(const ARCode& arcode)
{
    const u32* code = &arcode.Code[0];

    u32 offset = 0;
    u32 datareg = 0;
    u32 cond = 1;
    u32 condstack = 0;

    const u32* loopstart = code;
    u32 loopcount = 0;
    u32 loopcond = 1;
    u32 loopcondstack = 0;

    // TODO: does anything reset this??
    u32 c5count = 0;

    for (;;)
    {
        if (code > &arcode.Code[arcode.Code.size() - 1])
            // If the instruction pointer is past the end of the cheat code...
            break;

        u32 a = *code++;
        u32 b = *code++;

        u8 op = a >> 24;

        if ((op < 0xD0 && op != 0xC5) || op > 0xD2)
        {
            if (!cond)
            {
                if ((op & 0xF0) == 0xE0)
                {
                    for (u32 i = 0; i < b; i += 8)
                        code += 2;
                }

                continue;
            }
        }

        switch (op)
        {
        case16(0x00): // 32-bit write
            NDS.ARM7Write32((a & 0x0FFFFFFF) + offset, b);
            break;

        case16(0x10): // 16-bit write
            NDS.ARM7Write16((a & 0x0FFFFFFF) + offset, b & 0xFFFF);
            break;

        case16(0x20): // 8-bit write
            NDS.ARM7Write8((a & 0x0FFFFFFF) + offset, b & 0xFF);
            break;

        case16(0x30): // IF b > u32[a]
            {
                condstack <<= 1;
                condstack |= cond;

                u32 addr = a & 0x0FFFFFFF;
                if (!addr) addr = offset;
                u32 chk = NDS.ARM7Read32(addr);

                cond = (b > chk) ? 1:0;
            }
            break;

        case16(0x40): // IF b < u32[a]
            {
                condstack <<= 1;
                condstack |= cond;

                u32 addr = a & 0x0FFFFFFF;
                if (!addr) addr = offset;
                u32 chk = NDS.ARM7Read32(addr);

                cond = (b < chk) ? 1:0;
            }
            break;

        case16(0x50): // IF b == u32[a]
            {
                condstack <<= 1;
                condstack |= cond;

                u32 addr = a & 0x0FFFFFFF;
                if (!addr) addr = offset;
                u32 chk = NDS.ARM7Read32(addr);

                cond = (b == chk) ? 1:0;
            }
            break;

        case16(0x60): // IF b != u32[a]
            {
                condstack <<= 1;
                condstack |= cond;

                u32 addr = a & 0x0FFFFFFF;
                if (!addr) addr = offset;
                u32 chk = NDS.ARM7Read32(addr);

                cond = (b != chk) ? 1:0;
            }
            break;

        case16(0x70): // IF b.l > ((~b.h) & u16[a])
            {
                condstack <<= 1;
                condstack |= cond;

                u32 addr = a & 0x0FFFFFFF;
                if (!addr) addr = offset;
                u16 val = NDS.ARM7Read16(addr);
                u16 chk = ~(b >> 16);
                chk &= val;

                cond = ((b & 0xFFFF) > chk) ? 1:0;
            }
            break;

        case16(0x80): // IF b.l < ((~b.h) & u16[a])
            {
                condstack <<= 1;
                condstack |= cond;

                u32 addr = a & 0x0FFFFFFF;
                if (!addr) addr = offset;
                u16 val = NDS.ARM7Read16(addr);
                u16 chk = ~(b >> 16);
                chk &= val;

                cond = ((b & 0xFFFF) < chk) ? 1:0;
            }
            break;

        case16(0x90): // IF b.l == ((~b.h) & u16[a])
            {
                condstack <<= 1;
                condstack |= cond;

                u32 addr = a & 0x0FFFFFFF;
                if (!addr) addr = offset;
                u16 val = NDS.ARM7Read16(addr);
                u16 chk = ~(b >> 16);
                chk &= val;

                cond = ((b & 0xFFFF) == chk) ? 1:0;
            }
            break;

        case16(0xA0): // IF b.l != ((~b.h) & u16[a])
            {
                condstack <<= 1;
                condstack |= cond;

                u32 addr = a & 0x0FFFFFFF;
                if (!addr) addr = offset;
                u16 val = NDS.ARM7Read16(addr);
                u16 chk = ~(b >> 16);
                chk &= val;

                cond = ((b & 0xFFFF) != chk) ? 1:0;
            }
            break;

        case16(0xB0): // offset = u32[a + offset]
            offset = NDS.ARM7Read32((a & 0x0FFFFFFF) + offset);
            break;

        case 0xC0: // FOR 0..b
            loopstart = code; // points to the first opcode after the FOR
            loopcount = b;
            loopcond = cond;           // checkme
            loopcondstack = condstack; // (GBAtek is not very clear there)
            break;

        case 0xC4: // offset = pointer to C4000000 opcode
            // theoretically used for safe storage, by accessing [offset+4]
            // in practice could be used for a self-modifying AR code
            // could be implemented with some hackery, but, does anything even
            // use it??
            Log(LogLevel::Error, "AR: !! THE FUCKING C4000000 OPCODE. TELL ARISOTURA.\n");
            return;

        case 0xC5: // count++ / IF (count & b.l) == b.h
            {
                // with weird condition checking, apparently
                // oh well

                c5count++;
                if (!cond) break;

                condstack <<= 1;
                condstack |= cond;

                u16 mask = b & 0xFFFF;
                u16 chk = b >> 16;

                cond = ((c5count & mask) == chk) ? 1:0;
            }
            break;

        case 0xC6: // u32[b] = offset
            NDS.ARM7Write32(b, offset);
            break;

        case 0xD0: // ENDIF
            cond = condstack & 0x1;
            condstack >>= 1;
            break;

        case 0xD1: // NEXT
            if (loopcount > 0)
            {
                loopcount--;
                code = loopstart;
            }
            else
            {
                cond = loopcond;
                condstack = loopcondstack;
            }
            break;

        case 0xD2: // NEXT+FLUSH
            if (loopcount > 0)
            {
                loopcount--;
                code = loopstart;
            }
            else
            {
                offset = 0;
                datareg = 0;
                condstack = 0;
                cond = 1;
            }
            break;

        case 0xD3: // offset = b
            offset = b;
            break;

        case 0xD4: // datareg += b
            datareg += b;
            break;

        case 0xD5: // datareg = b
            datareg = b;
            break;

        case 0xD6: // u32[b+offset] = datareg / offset += 4
            NDS.ARM7Write32(b + offset, datareg);
            offset += 4;
            break;

        case 0xD7: // u16[b+offset] = datareg / offset += 2
            NDS.ARM7Write16(b + offset, datareg & 0xFFFF);
            offset += 2;
            break;

        case 0xD8: // u8[b+offset] = datareg / offset += 1
            NDS.ARM7Write8(b + offset, datareg & 0xFF);
            offset += 1;
            break;

        case 0xD9: // datareg = u32[b+offset]
            datareg = NDS.ARM7Read32(b + offset);
            break;

        case 0xDA: // datareg = u16[b+offset]
            datareg = NDS.ARM7Read16(b + offset);
            break;

        case 0xDB: // datareg = u8[b+offset]
            datareg = NDS.ARM7Read8(b + offset);
            break;

        case 0xDC: // offset += b
            offset += b;
            break;

        case16(0xE0): // copy b param bytes to address a+offset
            {
                // TODO: check for bad alignment of dstaddr

                u32 dstaddr = (a & 0x0FFFFFFF) + offset;
                u32 bytesleft = b;
                while (bytesleft >= 8)
                {
                    NDS.ARM7Write32(dstaddr, *code++); dstaddr += 4;
                    NDS.ARM7Write32(dstaddr, *code++); dstaddr += 4;
                    bytesleft -= 8;
                }
                if (bytesleft > 0)
                {
                    u8* leftover = (u8*)code;
                    code += 2;
                    if (bytesleft >= 4)
                    {
                        NDS.ARM7Write32(dstaddr, *(u32*)leftover); dstaddr += 4;
                        leftover += 4;
                        bytesleft -= 4;
                    }
                    while (bytesleft > 0)
                    {
                        NDS.ARM7Write8(dstaddr, *leftover++); dstaddr++;
                        bytesleft--;
                    }
                }
            }
            break;

        case16(0xF0): // copy b bytes from address offset to address a
            {
                // TODO: check for bad alignment of srcaddr/dstaddr

                u32 srcaddr = offset;
                u32 dstaddr = (a & 0x0FFFFFFF);
                u32 bytesleft = b;
                while (bytesleft >= 4)
                {
                    NDS.ARM7Write32(dstaddr, NDS.ARM7Read32(srcaddr));
                    srcaddr += 4;
                    dstaddr += 4;
                    bytesleft -= 4;
                }
                while (bytesleft > 0)
                {
                    NDS.ARM7Write8(dstaddr, NDS.ARM7Read8(srcaddr));
                    srcaddr++;
                    dstaddr++;
                    bytesleft--;
                }
            }
            break;

        default:
            Log(LogLevel::Warn, "!! bad AR opcode %08X %08X\n", a, b);
            return;
        }
    }
}

void AREngine::RunCheats()
{
    if (Cheats.empty()) return;

    for (const ARCode& code : Cheats)
    {
        if (code.Enabled)
            RunCheat(code);
    }
}
}
