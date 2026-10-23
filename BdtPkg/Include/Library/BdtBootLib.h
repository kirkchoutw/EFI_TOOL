/** @file
  BdtBootLib - boot option / boot performance inspection.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_BOOT_LIB_H_
#define BDT_BOOT_LIB_H_

#include <Uefi.h>

EFI_STATUS EFIAPI BdtBootOrder (VOID);
EFI_STATUS EFIAPI BdtBootOptions (VOID);
EFI_STATUS EFIAPI BdtBootDump (IN UINT16 Number);

/**
  Show the firmware performance data (ACPI FPDT / FBPT).
**/
EFI_STATUS EFIAPI BdtBootTiming (VOID);

/**
  Show the loaded images in load order (handle database order).
**/
EFI_STATUS EFIAPI BdtBootModules (VOID);

#endif
