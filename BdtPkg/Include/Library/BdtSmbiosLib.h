/** @file
  BdtSmbiosLib - SMBIOS inspection.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_SMBIOS_LIB_H_
#define BDT_SMBIOS_LIB_H_

#include <Uefi.h>

EFI_STATUS EFIAPI BdtSmbiosGetVersion (OUT UINT8 *Major, OUT UINT8 *Minor, OUT UINT8 *DocRev);

/**
  Return the BIOS vendor / version / release date strings (SMBIOS Type 0).
  Missing strings are returned as empty strings.
**/
EFI_STATUS EFIAPI BdtSmbiosGetBiosInfo (
  OUT CHAR16  *Vendor,
  IN  UINTN   VendorSize,
  OUT CHAR16  *Version,
  IN  UINTN   VersionSize,
  OUT CHAR16  *Date,
  IN  UINTN   DateSize
  );

EFI_STATUS EFIAPI BdtSmbiosList (VOID);

/**
  Decode every structure (Type = MAX_UINT16) or only structures of one type.
**/
EFI_STATUS EFIAPI BdtSmbiosDump (IN UINT16 Type);
EFI_STATUS EFIAPI BdtSmbiosValidate (VOID);

/**
  Get structure Index (0 based) of the SMBIOS table.

  @param[out] Structure  Start of the structure (formatted area).
  @param[out] TotalSize  Formatted area plus string set, including the double NULL.

  @retval EFI_NOT_FOUND  Index past the end of the table.
**/
EFI_STATUS EFIAPI BdtSmbiosGetStructure (IN UINTN Index, OUT VOID **Structure, OUT UINTN *TotalSize);

CONST CHAR16 * EFIAPI BdtSmbiosTypeName (IN UINT8 Type);

#endif
