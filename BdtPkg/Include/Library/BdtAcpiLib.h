/** @file
  BdtAcpiLib - ACPI table inspection.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_ACPI_LIB_H_
#define BDT_ACPI_LIB_H_

#include <Uefi.h>

#include <IndustryStandard/Acpi.h>

/**
  Get the ACPI table at Index (0 based) in the order RSDT/XSDT, tables listed in
  XSDT (or RSDT), then FACS and DSDT referenced by the FADT.

  @retval EFI_NOT_FOUND  Index past the end or no ACPI.
**/
EFI_STATUS EFIAPI BdtAcpiGetTable (IN UINTN Index, OUT EFI_ACPI_DESCRIPTION_HEADER **Table);

/**
  Find the Instance-th (0 based) table with Signature.
**/
EFI_STATUS EFIAPI BdtAcpiFindTable (IN UINT32 Signature, IN UINTN Instance, OUT EFI_ACPI_DESCRIPTION_HEADER **Table);

/**
  Return the ACPI version ("6.5") and number of tables.
**/
EFI_STATUS EFIAPI BdtAcpiGetInfo (OUT CHAR16 *Version, IN UINTN VersionSize, OUT UINTN *TableCount);

EFI_STATUS EFIAPI BdtAcpiList (VOID);
EFI_STATUS EFIAPI BdtAcpiDump (IN CONST CHAR16 *Signature, IN UINTN Instance);
EFI_STATUS EFIAPI BdtAcpiFind (IN CONST CHAR16 *String);
EFI_STATUS EFIAPI BdtAcpiValidate (VOID);

/**
  Save every table as raw binary into Directory ("DSDT.aml", "SSDT1.aml", ...).
**/
EFI_STATUS EFIAPI BdtAcpiSaveTables (IN CONST CHAR16 *Directory);

#endif
