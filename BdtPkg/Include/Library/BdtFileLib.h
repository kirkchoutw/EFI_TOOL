/** @file
  BdtFileLib - file and directory helpers on top of the UEFI Shell.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_FILE_LIB_H_
#define BDT_FILE_LIB_H_

#include <Uefi.h>
#include <Library/ShellLib.h>

/**
  Create a directory. Succeeds if it already exists.
**/
EFI_STATUS
EFIAPI
BdtFileCreateDirectory (
  IN CONST CHAR16  *Path
  );

/**
  Create (or truncate) a file and open it for writing.
**/
EFI_STATUS
EFIAPI
BdtFileCreate (
  IN  CONST CHAR16       *Path,
  OUT SHELL_FILE_HANDLE  *Handle
  );

EFI_STATUS
EFIAPI
BdtFileWrite (
  IN SHELL_FILE_HANDLE  Handle,
  IN CONST VOID         *Buffer,
  IN UINTN              Size
  );

VOID
EFIAPI
BdtFileClose (
  IN SHELL_FILE_HANDLE  Handle
  );

/**
  Create a file and write a whole buffer to it.
**/
EFI_STATUS
EFIAPI
BdtFileWriteAll (
  IN CONST CHAR16  *Path,
  IN CONST VOID    *Buffer,
  IN UINTN         Size
  );

/**
  Read a whole file into a newly allocated buffer.
**/
EFI_STATUS
EFIAPI
BdtFileReadAll (
  IN  CONST CHAR16  *Path,
  OUT VOID          **Buffer,
  OUT UINTN         *Size
  );

#endif
