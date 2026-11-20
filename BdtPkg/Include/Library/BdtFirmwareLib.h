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

/**
  Show the SHA-256 of every FV (raw volume) and FFS file (file contents) of
  one FV (1-based FvIndex) or of all FVs when 0.
**/
EFI_STATUS EFIAPI BdtFirmwareHash (IN UINTN FvIndex);

/**
  Write the hashes of all FVs and FFS files to a text manifest.
**/
EFI_STATUS EFIAPI BdtFirmwareManifest (IN CONST CHAR16 *Path);

/**
  Compare the running firmware with a manifest written by
  BdtFirmwareManifest.

  @retval EFI_SUCCESS             Everything matches (unreadable data aside).
  @retval EFI_SECURITY_VIOLATION  Something changed, is missing or is new.
**/
EFI_STATUS EFIAPI BdtFirmwareVerify (IN CONST CHAR16 *Path);

#endif
