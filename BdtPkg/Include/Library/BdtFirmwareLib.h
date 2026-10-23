/** @file
  BdtFirmwareLib - firmware volume / FFS inspection.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_FIRMWARE_LIB_H_
#define BDT_FIRMWARE_LIB_H_

#include <Uefi.h>

EFI_STATUS EFIAPI BdtFirmwareFvList (VOID);

/**
  List the files of one firmware volume (1-based FvIndex) or all when 0.
**/
EFI_STATUS EFIAPI BdtFirmwareFfsList (IN UINTN FvIndex);
EFI_STATUS EFIAPI BdtFirmwareFind (IN CONST EFI_GUID *FileName);

#endif
