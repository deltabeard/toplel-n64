/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - main.c                                          *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2012 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_types.h"
#include "m64p_common.h"
#include "m64p_plugin.h"
#include "hle.h"
#include "alist.h"
#include "cicx105.h"
#include "jpeg.h"
#include "musyx.h"

#define min(a,b) (((a) < (b)) ? (a) : (b))

/* some rsp status flags */
#define RSP_STATUS_HALT             0x1
#define RSP_STATUS_BROKE            0x2
#define RSP_STATUS_INTR_ON_BREAK    0x40
#define RSP_STATUS_TASKDONE         0x200

/* some rdp status flags */
#define DP_STATUS_FREEZE            0x2

/* some mips interface interrupt flags */
#define MI_INTR_SP                  0x1


/* helper functions prototypes */
static uint32_t sum_bytes(const uint8_t *bytes, uint32_t size);
static void dump_binary(const int8_t * const filename, const uint8_t* const bytes,
                        uint32_t size);
static void dump_task(const char * const filename, const OSTask_t * const task);

static void handle_unknown_task(uint32_t sum);
static void handle_unknown_non_task(uint32_t sum);

/* global variables */
RSP_INFO rspInfo;

/* local variables */
static const int FORWARD_AUDIO = 0, FORWARD_GFX = 1;
static void (*l_DebugCallback)(void *, int, const int8_t *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;

/* local functions */


/**
 * Try to figure if the RSP was launched using osSpTask* functions
 * and not run directly (in which case DMEM[0xfc0-0xfff] is meaningless).
 *
 * Previously, the ucode_size field was used to determine this,
 * but it is not robust enough (hi Pokemon Stadium !) because games could write anything
 * in this field : most ucode_boot discard the value and just use 0xf7f anyway.
 *
 * Using ucode_boot_size should be more robust in this regard.
 **/
static int is_task(void)
{
    return (get_task()->ucode_boot_size <= 0x1000);
}

static void rsp_break(uint32_t setbits)
{
    *rspInfo.SP_STATUS_REG |= setbits | RSP_STATUS_BROKE | RSP_STATUS_HALT;

    if ((*rspInfo.SP_STATUS_REG & RSP_STATUS_INTR_ON_BREAK))
    {
        *rspInfo.MI_INTR_REG |= MI_INTR_SP;
        rspInfo.CheckInterrupts();
    }
}

static void forward_gfx_task(void)
{
    if (rspInfo.ProcessDlistList != NULL)
    {
        rspInfo.ProcessDlistList();
        *rspInfo.DPC_STATUS_REG &= ~DP_STATUS_FREEZE;
    }
}

static void forward_audio_task(void)
{
    if (rspInfo.ProcessAlistList != NULL)
    {
        printf("Fink\n");
        rspInfo.ProcessAlistList();
    }
    else
        printf("FANK\n");
}

static void show_cfb(void)
{
    if (rspInfo.ShowCFB != NULL)
        rspInfo.ShowCFB();
}

static int try_fast_audio_dispatching()
{
    /* identify audio ucode by using the content of ucode_data */
    uint32_t ucode_data = get_task()->ucode_data;

    if (*dram_u32(ucode_data) == 0x00000001)
    {
       if (*dram_u32(ucode_data + 0x30) == 0xf0000f00)
       {
            /**
            * Many games including:
            * Super Mario 64, Diddy Kong Racing, BlastCorp, GoldenEye, ... (most common)
            **/
            alist_process_ABI1(); return 1;
        }
        else
        {
            /**
            * Mario Kart / Wave Race,
            * LylatWars,
            * FZeroX,
            * Yoshi Story,
            * 1080 Snowboarding,
            * Zelda Ocarina of Time,
            * Zelda Majoras Mask / Pokemon Stadium 2,
            * Animal Crossing
            *
            * FIXME: in fact, all these games do not share the same ABI.
            * That's the reason of the workaround in ucode2.cpp with isZeldaABI and isMKABI
            **/
            alist_process_ABI2(); return 1;
        }
    }
    else
    {
        if (*dram_u32(ucode_data + 0x10) == 0x00000001)
        {
            /**
             * Musyx ucode found in following games:
             * RogueSquadron, ResidentEvil2, SnowCrossPolaris, TheWorldIsNotEnough,
             * RugratsInParis, NBAShowTime, HydroThunder, Tarzan,
             * GauntletLegend, Rush2049, IndianaJones, BattleForNaboo
             * TODO: implement ucode
             **/
            musyx_task();
            return 1;
        }
        else
        {
            /**
             * Many games including:
             * Pokemon Stadium, Banjo Kazooie, Donkey Kong, Banjo Tooie, Jet Force Gemini,
             * Mickey SpeedWay USA, Perfect Dark, Conker Bad Fur Day ...
             **/
            alist_process_ABI3(); return 1;
        }
    }
    
    return 0;
}

static int try_fast_task_dispatching()
{
    /* identify task ucode by its type */
    const OSTask_t * const task = get_task();

    switch (task->type)
    {
        case 1:
           if (FORWARD_GFX) { forward_gfx_task(); return 1; } break;

        case 2:
            if (FORWARD_AUDIO) { forward_audio_task(); return 1; }
            else if (try_fast_audio_dispatching()) { return 1; }
            break;

        case 7: show_cfb(); return 1;
    }

    return 0;
}

static void normal_task_dispatching()
{
    const OSTask_t * const task = get_task();
    const uint32_t sum = sum_bytes(dram_u8(task->ucode), min(task->ucode_size, 0xf80) >> 1);

    switch (sum)
    {
        /* StoreVe12: found in Zelda Ocarina of Time [misleading task->type == 4] */
        case 0x278: /* Nothing to emulate */ return;

        /* GFX: Twintris [misleading task->type == 0] */                                         
        case 0x212ee:
            if (FORWARD_GFX) { forward_gfx_task(); return; }
            break;

        /* JPEG: found in Pokemon Stadium J */ 
        case 0x2c85a: jpeg_decode_PS0(); return;

        /* JPEG: found in Zelda Ocarina of Time, Pokemon Stadium 1, Pokemon Stadium 2 */
        case 0x2caa6: jpeg_decode_PS(); return;

        /* JPEG: found in Ogre Battle, Bottom of the 9th */
        case 0x130de:
        case 0x278b0:
                      jpeg_decode_OB(); return;
    }

    handle_unknown_task(sum);
}

static void non_task_dispatching()
{
    const uint32_t sum = sum_bytes(rspInfo.IMEM, 0x1000 >> 1);

    switch(sum)
    {
        /* CIC x105 ucode (used during boot of CIC x105 games) */
        case 0x9e2: /* CIC 6105 */
        case 0x9f2: /* CIC 7105 */
            cicx105_ucode(); return;
    }

    handle_unknown_non_task(sum);
}

static void handle_unknown_task(uint32_t sum)
{
#if 0
    char filename[256];
    const OSTask_t * const task = get_task();

    RSP_DEBUG_MESSAGE(M64MSG_WARNING, "unknown OSTask: sum %x PC:%x", sum, *rspInfo.SP_PC_REG);

    sprintf(&filename[0], "task_%x.log", sum);
    dump_task(filename, task);
    
    // dump ucode_boot
    sprintf(&filename[0], "ucode_boot_%x.bin", sum);
    dump_binary(filename, dram_u8(task->ucode_boot), task->ucode_boot_size);

    // dump ucode
    if (task->ucode != 0)
    {
        sprintf(&filename[0], "ucode_%x.bin", sum);
        dump_binary(filename, dram_u8(task->ucode), 0xf80);
    }

    // dump ucode_data
    if (task->ucode_data != 0)
    {
        sprintf(&filename[0], "ucode_data_%x.bin", sum);
        dump_binary(filename, dram_u8(task->ucode_data), task->ucode_data_size);
    }

    // dump data
    if (task->data_ptr != 0)
    {
        sprintf(&filename[0], "data_%x.bin", sum);
        dump_binary(filename, dram_u8(task->data_ptr), task->data_size);
    }
#endif
}

static void handle_unknown_non_task(uint32_t sum)
{
#if 0
    char filename[256];

    RSP_DEBUG_MESSAGE(M64MSG_WARNING, "unknown RSP code: sum: %x PC:%x", sum, *rspInfo.SP_PC_REG);

    // dump IMEM & DMEM for further analysis
    sprintf(&filename[0], "imem_%x.bin", sum);
    dump_binary(filename, rspInfo.IMEM, 0x1000);

    sprintf(&filename[0], "dmem_%x.bin", sum);
    dump_binary(filename, rspInfo.DMEM, 0x1000);
#endif
}


/* DLL-exported functions */
EXPORT m64p_error CALL rspPluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                   void (*DebugCallback)(void *, int, const char *))
{
    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    /* first thing is to set the callback function for debug info */
    l_DebugCallback = DebugCallback;
    l_DebugCallContext = Context;

    /* this plugin doesn't use any Core library functions (ex for Configuration), so no need to keep the CoreLibHandle */

    l_PluginInit = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL rspPluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

    /* reset some local variable */
    l_DebugCallback = NULL;
    l_DebugCallContext = NULL;

    l_PluginInit = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL hlePluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_RSP;

    if (PluginVersion != NULL)
        *PluginVersion = RSP_HLE_VERSION;

    if (APIVersion != NULL)
        *APIVersion = RSP_PLUGIN_API_VERSION;
    
    if (PluginNamePtr != NULL)
        *PluginNamePtr = "Hacktarux/Azimer High-Level Emulation RSP Plugin";

    if (Capabilities != NULL)
    {
        *Capabilities = 0;
    }
                    
    return M64ERR_SUCCESS;
}

EXPORT unsigned int CALL hleDoRspCycles(uint32_t Cycles)
{
    if (is_task())
    {
        if (!try_fast_task_dispatching()) { normal_task_dispatching(); }
        rsp_break(RSP_STATUS_TASKDONE);
    }
    else
    {
        non_task_dispatching();
        rsp_break(0);
    }

    return Cycles;
}

EXPORT void CALL hleInitiateRSP(RSP_INFO Rsp_Info, uint32_t *CycleCount)
{
    rspInfo = Rsp_Info;
}

EXPORT void CALL hleRomClosed(void)
{
    memset(rspInfo.DMEM, 0, 0x1000);
    memset(rspInfo.IMEM, 0, 0x1000);

    init_ucode2();
}


/* local helper functions */
static uint32_t sum_bytes(const uint8_t *bytes, uint32_t size)
{
    uint32_t sum = 0;
    const uint8_t * const bytes_end = bytes + size;

    while (bytes != bytes_end)
        sum += *bytes++;

    return sum;
}


static void dump_binary(const int8_t * const filename, const uint8_t * const bytes,
                        uint32_t size)
{
#if 0
    FILE *f;

    // if file already exists, do nothing
    f = fopen(filename, "r");
    if (f == NULL)
    {
        // else we write bytes to the file
        f= fopen(filename, "wb");
        if (f != NULL) {
            if (fwrite(bytes, 1, size, f) != size)
            {
                RSP_DEBUG_MESSAGE(M64MSG_ERROR, "Writing error on %s", filename);
            }
            fclose(f);
        }
#ifndef DEBUG
        else
        {
            RSP_DEBUG_MESSAGE(M64MSG_ERROR, "Couldn't open %s for writing !", filename);
        }
#endif
    }
    else
    {
        fclose(f);
    }
#endif
}

static void dump_task(const char * const filename, const OSTask_t * const task)
{
#if 0
    FILE *f;

    f = fopen(filename, "r");
    if (f == NULL)
    {
        f = fopen(filename, "w");
        fprintf(f,
            "type = %d\n"
            "flags = %d\n"
            "ucode_boot  = %#08x size  = %#x\n"
            "ucode       = %#08x size  = %#x\n"
            "ucode_data  = %#08x size  = %#x\n"
            "dram_stack  = %#08x size  = %#x\n"
            "output_buff = %#08x *size = %#x\n"
            "data        = %#08x size  = %#x\n"
            "yield_data  = %#08x size  = %#x\n",
            task->type, task->flags,
            task->ucode_boot, task->ucode_boot_size,
            task->ucode, task->ucode_size,
            task->ucode_data, task->ucode_data_size,
            task->dram_stack, task->dram_stack_size,
            task->output_buff, task->output_buff_size,
            task->data_ptr, task->data_size,
            task->yield_data_ptr, task->yield_data_size);
        fclose(f);
    }
    else
    {
        fclose(f);
    }
#endif
}

/* memory access helper functions */
void dmem_load_u8 (uint8_t* dst, uint16_t address, size_t count)
{
    while (count != 0) {
        *(dst++) = *dmem_u8(address);
        address += 1;
        --count;
    }
}

void dmem_load_u16(uint16_t* dst, uint16_t address, size_t count)
{
    while (count != 0) {
        *(dst++) = *dmem_u16(address);
        address += 2;
        --count;
    }
}

void dmem_load_u32(uint32_t* dst, uint16_t address, size_t count)
{
    /* Optimization for uint32_t */
    memcpy(dst, dmem_u32(address), count * sizeof(uint32_t));
}

void dmem_store_u8 (const uint8_t* src, uint16_t address, size_t count)
{
    while (count != 0) {
        *dmem_u8(address) = *(src++);
        address += 1;
        --count;
    }
}

void dmem_store_u16(const uint16_t* src, uint16_t address, size_t count)
{
    while (count != 0) {
        *dmem_u16(address) = *(src++);
        address += 2;
        --count;
    }
}

void dmem_store_u32(const uint32_t* src, uint16_t address, size_t count)
{
    /* Optimization for uint32_t */
    memcpy(dmem_u32(address), src, count * sizeof(uint32_t));
}


void dram_load_u8 (uint8_t* dst, uint32_t address, size_t count)
{
    while (count != 0) {
        *(dst++) = *dram_u8(address);
        address += 1;
        --count;
    }
}

void dram_load_u16(uint16_t* dst, uint32_t address, size_t count)
{
    while (count != 0) {
        *(dst++) = *dram_u16(address);
        address += 2;
        --count;
    }
}

void dram_load_u32(uint32_t* dst, uint32_t address, size_t count)
{
    /* Optimization for uint32_t */
    memcpy(dst, dram_u32(address), count * sizeof(uint32_t));
}

void dram_store_u8 (const uint8_t* src, uint32_t address, size_t count)
{
    while (count != 0) {
        *dram_u8(address) = *(src++);
        address += 1;
        --count;
    }
}

void dram_store_u16(const uint16_t* src, uint32_t address, size_t count)
{
    while (count != 0) {
        *dram_u16(address) = *(src++);
        address += 2;
        --count;
    }
}

void dram_store_u32(const uint32_t* src, uint32_t address, size_t count)
{
    /* Optimization for uint32_t */
    memcpy(dram_u32(address), src, count * sizeof(uint32_t));
}
