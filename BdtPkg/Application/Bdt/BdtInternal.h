/** @file
  BDT.efi - internal definitions shared by the application source files.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_INTERNAL_H_
#define BDT_INTERNAL_H_

#include <BdtCommand.h>

#include <Library/ShellCEntryLib.h>
#include <Library/ShellLib.h>

#include <Library/BdtAcpiLib.h>
#include <Library/BdtBootLib.h>
#include <Library/BdtCpuLib.h>
#include <Library/BdtFileLib.h>
#include <Library/BdtFirmwareLib.h>
#include <Library/BdtHandleLib.h>
#include <Library/BdtMemoryLib.h>
#include <Library/BdtPciLib.h>
#include <Library/BdtSecurityLib.h>
#include <Library/BdtSmbiosLib.h>
#include <Library/BdtVariableLib.h>

//
// Command handlers (BdtCommands.c / BdtSystem.c / BdtCollect.c).
//
EFI_STATUS BdtCmdHelp       (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdVersion    (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdSystem     (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdBoot       (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdPci        (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdMemory     (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdVar        (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdHandle     (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdProtocol   (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdDevicePath (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdAcpi       (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdSmbios     (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdCpu        (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdFirmware   (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdSecurity   (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdIo         (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdCollect    (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);
EFI_STATUS BdtCmdUi         (IN BDT_CONTEXT *Context, IN UINTN Argc, IN CHAR16 **Argv);

/**
  Emit the "system" summary (no document wrapper).
**/
EFI_STATUS
BdtSystemSummary (
  VOID
  );

/**
  Open the output sink (console or --output file) and begin a document.
**/
EFI_STATUS
BdtBeginOutput (
  IN BDT_CONTEXT   *Context,
  IN CONST CHAR16  *Command
  );

/**
  End the document, close the output file and pass Status through.
**/
EFI_STATUS
BdtEndOutput (
  IN EFI_STATUS  Status
  );

extern CONST BDT_COMMAND  gBdtCommands[];
extern CONST UINTN        gBdtCommandCount;

#endif
