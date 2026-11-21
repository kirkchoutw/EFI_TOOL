/** @file
  BdtLogLib - firmware debug log (in memory log buffers).

  UEFI has no standard debug log buffer. BdtLogLib reads the formats that
  firmware actually uses through pluggable backends:

    AdvancedLogger  Project Mu AdvLoggerPkg (many Microsoft / OEM platforms)
    MemDebugLog     edk2 OvmfPkg MemDebugLogLib (DEBUG_TO_MEM=TRUE)

  The first backend that finds its buffer is used.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_LOG_LIB_H_
#define BDT_LOG_LIB_H_

#include <Uefi.h>

/**
  List the backends and what each one found.
**/
EFI_STATUS EFIAPI BdtLogInfo (VOID);

/**
  Show the log. Last = 0 shows everything, otherwise the last Last lines.
**/
EFI_STATUS EFIAPI BdtLogShow (IN UINTN Last);

/**
  Line count, errors / warnings / ASSERTs and the lines that contain them.
**/
EFI_STATUS EFIAPI BdtLogSummary (VOID);

/**
  Write the log as plain text to a file.
**/
EFI_STATUS EFIAPI BdtLogExport (IN CONST CHAR16 *Path);

/**
  Empty the log buffer (a memory write). Asks [y/N] unless Force.

  @retval EFI_ABORTED  The user said no.
**/
EFI_STATUS EFIAPI BdtLogClear (IN BOOLEAN Force);

#endif
