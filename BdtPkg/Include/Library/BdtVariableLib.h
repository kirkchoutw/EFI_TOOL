/** @file
  BdtVariableLib - UEFI variable inspection.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_VARIABLE_LIB_H_
#define BDT_VARIABLE_LIB_H_

#include <Uefi.h>

EFI_STATUS EFIAPI BdtVarList (VOID);

/**
  Show one variable. Full = FALSE limits the hex dump to the first 256 bytes.
**/
EFI_STATUS EFIAPI BdtVarGet (IN CONST CHAR16 *Name, IN CONST EFI_GUID *Guid, IN BOOLEAN Full);
EFI_STATUS EFIAPI BdtVarUsage (VOID);

/**
  Write every variable as a binary file "<Name>-<Guid>.bin" under Directory.
**/
EFI_STATUS EFIAPI BdtVarSaveAll (IN CONST CHAR16 *Directory);

/**
  Compare two saved variable data files (from "var save" / collect) byte by byte.
**/
EFI_STATUS EFIAPI BdtVarDiffFiles (IN CONST CHAR16 *File1, IN CONST CHAR16 *File2);

#endif
