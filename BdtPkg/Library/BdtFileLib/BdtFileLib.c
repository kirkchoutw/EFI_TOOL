/** @file
  BdtFileLib - file and directory helpers on top of the UEFI Shell.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/ShellLib.h>
#include <Library/BdtFileLib.h>

EFI_STATUS
EFIAPI
BdtFileCreateDirectory (
  IN CONST CHAR16  *Path
  )
{
  EFI_STATUS         Status;
  SHELL_FILE_HANDLE  Handle;

  if (ShellIsDirectory (Path) == EFI_SUCCESS) {
    return EFI_SUCCESS;
  }

  Handle = NULL;
  Status = ShellCreateDirectory (Path, &Handle);
  if (!EFI_ERROR (Status) && (Handle != NULL)) {
    ShellCloseFile (&Handle);
  }

  return Status;
}

EFI_STATUS
EFIAPI
BdtFileCreate (
  IN  CONST CHAR16       *Path,
  OUT SHELL_FILE_HANDLE  *Handle
  )
{
  if (ShellFileExists (Path) == EFI_SUCCESS) {
    ShellDeleteFileByName (Path);
  }

  return ShellOpenFileByName (
           Path,
           Handle,
           EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
           0
           );
}

EFI_STATUS
EFIAPI
BdtFileWrite (
  IN SHELL_FILE_HANDLE  Handle,
  IN CONST VOID         *Buffer,
  IN UINTN              Size
  )
{
  return ShellWriteFile (Handle, &Size, (VOID *)Buffer);
}

VOID
EFIAPI
BdtFileClose (
  IN SHELL_FILE_HANDLE  Handle
  )
{
  if (Handle != NULL) {
    ShellCloseFile (&Handle);
  }
}

EFI_STATUS
EFIAPI
BdtFileWriteAll (
  IN CONST CHAR16  *Path,
  IN CONST VOID    *Buffer,
  IN UINTN         Size
  )
{
  EFI_STATUS         Status;
  SHELL_FILE_HANDLE  Handle;

  Status = BdtFileCreate (Path, &Handle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = BdtFileWrite (Handle, Buffer, Size);
  BdtFileClose (Handle);
  return Status;
}

EFI_STATUS
EFIAPI
BdtFileReadAll (
  IN  CONST CHAR16  *Path,
  OUT VOID          **Buffer,
  OUT UINTN         *Size
  )
{
  EFI_STATUS         Status;
  SHELL_FILE_HANDLE  Handle;
  UINT64             FileSize;
  UINTN              ReadSize;
  VOID               *Data;

  *Buffer = NULL;
  *Size   = 0;

  Status = ShellOpenFileByName (Path, &Handle, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ShellGetFileSize (Handle, &FileSize);
  if (EFI_ERROR (Status) || (FileSize > MAX_UINTN - 1)) {
    ShellCloseFile (&Handle);
    return EFI_ERROR (Status) ? Status : EFI_BAD_BUFFER_SIZE;
  }

  Data = AllocateZeroPool ((UINTN)FileSize + 1);
  if (Data == NULL) {
    ShellCloseFile (&Handle);
    return EFI_OUT_OF_RESOURCES;
  }

  ReadSize = (UINTN)FileSize;
  Status   = ShellReadFile (Handle, &ReadSize, Data);
  ShellCloseFile (&Handle);
  if (EFI_ERROR (Status)) {
    FreePool (Data);
    return Status;
  }

  *Buffer = Data;
  *Size   = ReadSize;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtFileListDirectory (
  IN  CONST CHAR16  *Path,
  OUT CHAR16        ***Names,
  OUT UINTN         *Count
  )
{
  EFI_STATUS         Status;
  SHELL_FILE_HANDLE  Dir;
  EFI_FILE_INFO      *Info;
  BOOLEAN            NoFile;
  CHAR16             **List;
  CHAR16             **NewList;
  UINTN              Size;
  UINTN              Used;

  *Names = NULL;
  *Count = 0;
  if (ShellIsDirectory (Path) != EFI_SUCCESS) {
    return EFI_NOT_FOUND;
  }

  Status = ShellOpenFileByName (Path, &Dir, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Size = 32;
  Used = 0;
  List = AllocateZeroPool (Size * sizeof (CHAR16 *));
  if (List == NULL) {
    ShellCloseFile (&Dir);
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Walk to the end: ShellFindNextFile frees Info after the last entry.
  //
  NoFile = FALSE;
  for (Status = ShellFindFirstFile (Dir, &Info);
       !EFI_ERROR (Status) && (Info != NULL) && !NoFile;
       Status = ShellFindNextFile (Dir, Info, &NoFile))
  {
    if ((Info->Attribute & EFI_FILE_DIRECTORY) != 0) {
      continue;
    }

    if (Used == Size) {
      NewList = ReallocatePool (Size * sizeof (CHAR16 *), Size * 2 * sizeof (CHAR16 *), List);
      if (NewList == NULL) {
        continue;
      }

      List  = NewList;
      Size *= 2;
    }

    List[Used] = AllocateCopyPool (StrSize (Info->FileName), Info->FileName);
    if (List[Used] != NULL) {
      Used++;
    }
  }

  ShellCloseFile (&Dir);
  *Names = List;
  *Count = Used;
  return EFI_SUCCESS;
}

VOID
EFIAPI
BdtFileFreeList (
  IN CHAR16  **Names,
  IN UINTN   Count
  )
{
  UINTN  Index;

  if (Names == NULL) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (Names[Index] != NULL) {
      FreePool (Names[Index]);
    }
  }

  FreePool (Names);
}
