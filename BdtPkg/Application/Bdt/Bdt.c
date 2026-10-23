/** @file
  BDT.efi - BIOS EFI Debug Toolkit UEFI Shell application.

  Usage: BDT.efi <command> [sub command] [arguments] [--format text|csv|json] [--output <file>]

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include "BdtInternal.h"

CONST BDT_COMMAND  gBdtCommands[] = {
  {
    L"help",       L"Show help",
    L"help [command]",
    BdtCmdHelp
  },
  {
    L"version",    L"Show tool version",
    L"version",
    BdtCmdVersion
  },
  {
    L"system",     L"BIOS / CPU / memory / ACPI / SMBIOS / security summary",
    L"system",
    BdtCmdSystem
  },
  {
    L"boot",       L"Boot options and boot performance",
    L"boot order | option | dump <number> | timing | modules | trace",
    BdtCmdBoot
  },
  {
    L"pci",        L"PCI / PCIe inspection",
    L"pci list | tree | dump <BDF> [--ext] | cap <BDF> | bar <BDF> | link <BDF> | aer [BDF]",
    BdtCmdPci
  },
  {
    L"memory",     L"Memory map, dump and search",
    L"memory map | info | dump <address> <length> | search <pattern> [start] [length]\r\n"
    L"         pattern: hex bytes (DEADBEEF), 0x<value> (little endian) or --string <text>",
    BdtCmdMemory
  },
  {
    L"var",        L"UEFI variables",
    L"var list | get <name> [guid] | dump <name> [guid] | usage | save <directory> | diff <file1> <file2>",
    BdtCmdVar
  },
  {
    L"handle",     L"Handle database",
    L"handle list | info <index> | protocol <guid> | tree",
    BdtCmdHandle
  },
  {
    L"protocol",   L"Installed protocols",
    L"protocol list | find <guid>",
    BdtCmdProtocol
  },
  {
    L"devicepath", L"Device paths",
    L"devicepath list | decode <handle index>",
    BdtCmdDevicePath
  },
  {
    L"acpi",       L"ACPI tables",
    L"acpi list | dump <signature> [instance] | find <string> | validate | save <directory>",
    BdtCmdAcpi
  },
  {
    L"smbios",     L"SMBIOS tables",
    L"smbios list | dump | type <type> | validate",
    BdtCmdSmbios
  },
  {
    L"cpu",        L"CPUID / MSR / MTRR / MCA",
    L"cpu info | cpuid [leaf [subleaf]] | msr <address> | mtrr | mca",
    BdtCmdCpu
  },
  {
    L"firmware",   L"Firmware volumes and FFS files",
    L"firmware fv | ffs [fv index] | find <guid>",
    BdtCmdFirmware
  },
  {
    L"security",   L"Secure Boot and TPM",
    L"security secureboot | pk | kek | db | dbx | dbt | dbr | tpm | pcr [index] | tcglog",
    BdtCmdSecurity
  },
  {
    L"io",         L"Raw register reads (read only)",
    L"io mmio read <address> <length> [width] | pci read <BDF> <offset> [width] | msr read <address>",
    BdtCmdIo
  },
  {
    L"collect",    L"Collect a complete debug package",
    L"collect [--quick | --full] [--output <directory>]",
    BdtCmdCollect
  },
  {
    L"ui",         L"Full screen browser (PCI, memory, I/O, MSR, ACPI, SMBIOS, variables)",
    L"ui",
    BdtCmdUi
  },
};

CONST UINTN  gBdtCommandCount = ARRAY_SIZE (gBdtCommands);

STATIC SHELL_FILE_HANDLE  mOutputFile = NULL;

EFI_STATUS
BdtBeginOutput (
  IN BDT_CONTEXT   *Context,
  IN CONST CHAR16  *Command
  )
{
  EFI_STATUS  Status;

  if (Context->OutputPath != NULL) {
    Status = BdtFileCreate (Context->OutputPath, &mOutputFile);
    if (EFI_ERROR (Status)) {
      BdtOutError (L"Cannot create %s (%s)", Context->OutputPath, BdtStatusName (Status));
      mOutputFile = NULL;
      return Status;
    }

    BdtOutSetFile (mOutputFile);
  }

  BdtOutBegin (Command);
  return EFI_SUCCESS;
}

EFI_STATUS
BdtEndOutput (
  IN EFI_STATUS  Status
  )
{
  BdtOutEnd ();
  if (mOutputFile != NULL) {
    BdtOutSetFile (NULL);
    BdtFileClose (mOutputFile);
    mOutputFile = NULL;
  }

  return Status;
}

STATIC
CONST BDT_COMMAND *
FindCommand (
  IN CONST CHAR16  *Name
  )
{
  UINTN  Index;

  for (Index = 0; Index < gBdtCommandCount; Index++) {
    if (BdtStriCmp (Name, gBdtCommands[Index].Name) == 0) {
      return &gBdtCommands[Index];
    }
  }

  return NULL;
}

STATIC
VOID
PrintUsage (
  IN CONST BDT_COMMAND  *Command
  )
{
  ShellPrintEx (-1, -1, L"Usage: BDT.efi %s\r\n", Command->Usage);
}

EFI_STATUS
BdtCmdHelp (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  CONST BDT_COMMAND  *Command;
  UINTN              Index;

  if (Argc >= 1) {
    Command = FindCommand (Argv[0]);
    if (Command == NULL) {
      BdtOutError (L"Unknown command '%s'", Argv[0]);
      return EFI_INVALID_PARAMETER;
    }

    ShellPrintEx (-1, -1, L"%s - %s\r\n", Command->Name, Command->Summary);
    PrintUsage (Command);
    return EFI_SUCCESS;
  }

  ShellPrintEx (-1, -1, L"%s (BDT) %s\r\n\r\n", BDT_TOOL_NAME, BDT_VERSION_STRING);
  ShellPrintEx (-1, -1, L"Usage: BDT.efi <command> [sub command] [arguments] [options]\r\n\r\n");
  ShellPrintEx (-1, -1, L"Commands:\r\n");
  for (Index = 0; Index < gBdtCommandCount; Index++) {
    ShellPrintEx (-1, -1, L"  %-12s %s\r\n", gBdtCommands[Index].Name, gBdtCommands[Index].Summary);
  }

  ShellPrintEx (-1, -1, L"\r\nOptions:\r\n");
  ShellPrintEx (-1, -1, L"  --format, -f <text|csv|json>  Output format (default text)\r\n");
  ShellPrintEx (-1, -1, L"  --output, -o <path>           Write output to a file (collect: base directory)\r\n");
  ShellPrintEx (-1, -1, L"  --help, -h                    Show help\r\n");
  ShellPrintEx (-1, -1, L"\r\nRun \"BDT.efi help <command>\" for the sub commands of a command.\r\n");
  return EFI_SUCCESS;
}

EFI_STATUS
BdtCmdVersion (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  EFI_STATUS  Status;

  Status = BdtBeginOutput (Context, L"version");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  BdtOutField (L"Tool", L"%s", BDT_TOOL_NAME);
  BdtOutField (L"Version", L"%s", BDT_VERSION_STRING);
  BdtOutField (L"Build", L"%a %a", __DATE__, __TIME__);
 #if defined (MDE_CPU_X64)
  BdtOutField (L"Architecture", L"X64");
 #elif defined (MDE_CPU_IA32)
  BdtOutField (L"Architecture", L"IA32");
 #else
  BdtOutField (L"Architecture", L"Other");
 #endif
  return BdtEndOutput (EFI_SUCCESS);
}

/**
  UEFI Shell application entry point.
**/
INTN
EFIAPI
ShellAppMain (
  IN UINTN   Argc,
  IN CHAR16  **Argv
  )
{
  EFI_STATUS         Status;
  BDT_CONTEXT        Context;
  CHAR16             **Args;
  UINTN              ArgCount;
  UINTN              Index;
  CONST BDT_COMMAND  *Command;
  BOOLEAN            Help;

  Help = FALSE;
  ZeroMem (&Context, sizeof (Context));
  Context.Format = BdtFormatText;

  Args = AllocateZeroPool ((Argc + 1) * sizeof (CHAR16 *));
  if (Args == NULL) {
    return (INTN)EFI_OUT_OF_RESOURCES;
  }

  //
  // Strip global options; everything else is positional.
  //
  ArgCount = 0;
  for (Index = 1; Index < Argc; Index++) {
    if ((BdtStriCmp (Argv[Index], L"--format") == 0) || (BdtStriCmp (Argv[Index], L"-f") == 0)) {
      if ((Index + 1 >= Argc) || EFI_ERROR (BdtOutParseFormat (Argv[Index + 1], &Context.Format))) {
        BdtOutError (L"--format needs text, csv or json");
        FreePool (Args);
        return (INTN)EFI_INVALID_PARAMETER;
      }

      Index++;
    } else if ((BdtStriCmp (Argv[Index], L"--output") == 0) || (BdtStriCmp (Argv[Index], L"-o") == 0)) {
      if (Index + 1 >= Argc) {
        BdtOutError (L"--output needs a path");
        FreePool (Args);
        return (INTN)EFI_INVALID_PARAMETER;
      }

      Context.OutputPath = Argv[++Index];
    } else if ((BdtStriCmp (Argv[Index], L"--help") == 0) || (BdtStriCmp (Argv[Index], L"-h") == 0) ||
               (StrCmp (Argv[Index], L"-?") == 0))
    {
      Help = TRUE;
    } else {
      Args[ArgCount++] = Argv[Index];
    }
  }

  BdtOutSetFormat (Context.Format);

  if (Help || (ArgCount == 0)) {
    //
    // "BDT.efi -h" and "BDT.efi <command> -h" both show help.
    //
    Status = BdtCmdHelp (&Context, MIN (ArgCount, 1), Args);
    FreePool (Args);
    return (INTN)Status;
  }

  Command = FindCommand (Args[0]);
  if (Command == NULL) {
    BdtOutError (L"Unknown command '%s'. Run \"BDT.efi help\".", Args[0]);
    FreePool (Args);
    return (INTN)EFI_INVALID_PARAMETER;
  }

  Status = Command->Handler (&Context, ArgCount - 1, &Args[1]);
  if (Status == EFI_INVALID_PARAMETER) {
    PrintUsage (Command);
  } else if (EFI_ERROR (Status)) {
    BdtOutError (L"%s failed: %s", Command->Name, BdtStatusName (Status));
  }

  FreePool (Args);
  return (INTN)Status;
}
