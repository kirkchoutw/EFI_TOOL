/** @file
  BdtMemoryLib - memory map, memory dump / search and MMIO read.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_MEMORY_LIB_H_
#define BDT_MEMORY_LIB_H_

#include <Uefi.h>

/**
  Return the total size of system RAM described by the UEFI memory map.
**/
EFI_STATUS EFIAPI BdtMemoryGetTotal (OUT UINT64 *TotalBytes);

EFI_STATUS EFIAPI BdtMemoryMap (VOID);
EFI_STATUS EFIAPI BdtMemoryInfo (VOID);
EFI_STATUS EFIAPI BdtMemoryDump (IN UINT64 Address, IN UINTN Length);

/**
  Search RAM for a byte pattern. Length = 0 searches all RAM regions of the
  memory map (page 0 is always skipped).
**/
EFI_STATUS EFIAPI BdtMemorySearch (IN CONST UINT8 *Pattern, IN UINTN PatternSize, IN UINT64 Start, IN UINT64 Length);

/**
  Read MMIO with the given access width (1, 2, 4 or 8 bytes).
**/
EFI_STATUS EFIAPI BdtMmioRead (IN UINT64 Address, IN UINTN Length, IN UINTN Width);

#endif
