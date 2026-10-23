/** @file
  BdtLib - common helpers (argument parsing, GUID names, variables).

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_LIB_H_
#define BDT_LIB_H_

#include <Uefi.h>

/**
  Parse a number. "0x" prefix (or a trailing 'h') means hex, otherwise decimal.

  @retval EFI_SUCCESS            Parsed.
  @retval EFI_INVALID_PARAMETER  Not a number.
**/
EFI_STATUS
EFIAPI
BdtParseNumber (
  IN  CONST CHAR16  *String,
  OUT UINT64        *Value
  );

/**
  Parse a hex number, with or without "0x" prefix.
**/
EFI_STATUS
EFIAPI
BdtParseHex (
  IN  CONST CHAR16  *String,
  OUT UINT64        *Value
  );

/**
  Parse a PCI address "[Seg:]Bus:Dev.Func" (all hex).
**/
EFI_STATUS
EFIAPI
BdtParseBdf (
  IN  CONST CHAR16  *String,
  OUT UINT16        *Segment,
  OUT UINT8         *Bus,
  OUT UINT8         *Device,
  OUT UINT8         *Function
  );

/**
  Parse a GUID "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" or a known GUID name
  such as "gEfiGlobalVariableGuid" / "EFI_GLOBAL_VARIABLE".
**/
EFI_STATUS
EFIAPI
BdtParseGuid (
  IN  CONST CHAR16  *String,
  OUT EFI_GUID      *Guid
  );

/**
  Return a well known name for a GUID, or NULL when unknown.
**/
CONST CHAR16 *
EFIAPI
BdtGuidName (
  IN CONST EFI_GUID  *Guid
  );

/**
  Return the GUID name when known, otherwise the GUID formatted as text in
  the supplied buffer.
**/
CONST CHAR16 *
EFIAPI
BdtGuidToString (
  IN  CONST EFI_GUID  *Guid,
  OUT CHAR16          *Buffer,
  IN  UINTN           BufferSize
  );

/**
  Iterate the internal table of known GUIDs.

  @param[in]  Index  Zero based index.
  @param[out] Guid   GUID at the index.
  @param[out] Name   Name at the index.

  @retval TRUE   Entry returned.
  @retval FALSE  Index past the end.
**/
BOOLEAN
EFIAPI
BdtGetKnownGuid (
  IN  UINTN           Index,
  OUT CONST EFI_GUID  **Guid,
  OUT CONST CHAR16    **Name
  );

/**
  Read a UEFI variable into a newly allocated buffer.

  @param[in]  Name        Variable name.
  @param[in]  Guid        Vendor GUID.
  @param[out] Data        Allocated data; free with FreePool.
  @param[out] DataSize    Size of the data.
  @param[out] Attributes  Optional attributes.
**/
EFI_STATUS
EFIAPI
BdtReadVariable (
  IN  CONST CHAR16    *Name,
  IN  CONST EFI_GUID  *Guid,
  OUT VOID            **Data,
  OUT UINTN           *DataSize,
  OUT UINT32          *Attributes OPTIONAL
  );

/**
  Format a byte count as "64 GB" / "512 MB" / "12 KB" / "100 B".
**/
VOID
EFIAPI
BdtFormatSize (
  IN  UINT64  Bytes,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  );

/**
  Return the short name of an EFI_STATUS ("EFI_NOT_FOUND"...).
**/
CONST CHAR16 *
EFIAPI
BdtStatusName (
  IN EFI_STATUS  Status
  );

/**
  Case insensitive compare of two CHAR16 strings.
**/
INTN
EFIAPI
BdtStriCmp (
  IN CONST CHAR16  *First,
  IN CONST CHAR16  *Second
  );

#endif
