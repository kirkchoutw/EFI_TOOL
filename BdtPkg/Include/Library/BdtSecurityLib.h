/** @file
  BdtSecurityLib - Secure Boot / TPM 2.0 inspection.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_SECURITY_LIB_H_
#define BDT_SECURITY_LIB_H_

#include <Uefi.h>

EFI_STATUS EFIAPI BdtSecureBootGetState (OUT BOOLEAN *Enabled, OUT BOOLEAN *SetupMode);
BOOLEAN    EFIAPI BdtTpmIsPresent (VOID);

EFI_STATUS EFIAPI BdtSecureBoot (VOID);

/**
  Show a signature database. Name is "PK", "KEK", "db", "dbx", "dbt" or "dbr".
**/
EFI_STATUS EFIAPI BdtSecurityDb (IN CONST CHAR16 *Name);
EFI_STATUS EFIAPI BdtTpmInfo (VOID);

/**
  Read PCRs from all active banks. Index = MAX_UINT32 reads PCR 0..23.
**/
EFI_STATUS EFIAPI BdtTpmPcr (IN UINT32 Index);
EFI_STATUS EFIAPI BdtTcgLog (VOID);

#endif
