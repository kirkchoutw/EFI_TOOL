/** @file
  BDT.efi - "collect" command: write a complete debug package to disk.

    BDT_YYYYMMDD_HHMMSS/
      manifest.txt
      system/  boot/  pci/  acpi/  uefi/  cpu/  security/

  Every item is produced by the same library function as the interactive
  command, in the format selected with --format.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include "BdtInternal.h"

#define COLLECT_QUICK   1
#define COLLECT_NORMAL  2
#define COLLECT_FULL    3

typedef
EFI_STATUS
(*BDT_COLLECT_FN)(
  IN CONST CHAR16  *Root
  );

typedef struct {
  UINT8             Level;
  CONST CHAR16      *Path;      ///< relative, without extension
  CONST CHAR16      *Command;   ///< equivalent interactive command
  BDT_COLLECT_FN    Collect;
} BDT_COLLECT_ITEM;

STATIC CHAR16  mBinaryPath[512];

//
// Wrappers adapting the library API to BDT_COLLECT_FN.
//
STATIC EFI_STATUS CollectSystem (IN CONST CHAR16 *Root) { return BdtSystemSummary (); }
STATIC EFI_STATUS CollectCpuInfo (IN CONST CHAR16 *Root) { return BdtCpuInfo (); }
STATIC EFI_STATUS CollectMemoryInfo (IN CONST CHAR16 *Root) { return BdtMemoryInfo (); }
STATIC EFI_STATUS CollectMemoryMap (IN CONST CHAR16 *Root) { return BdtMemoryMap (); }
STATIC EFI_STATUS CollectSmbios (IN CONST CHAR16 *Root) { return BdtSmbiosDump (MAX_UINT16); }
STATIC EFI_STATUS CollectSmbiosValidate (IN CONST CHAR16 *Root) { return BdtSmbiosValidate (); }
STATIC EFI_STATUS CollectFirmwareFv (IN CONST CHAR16 *Root) { return BdtFirmwareFvList (); }
STATIC EFI_STATUS CollectFirmwareFfs (IN CONST CHAR16 *Root) { return BdtFirmwareFfsList (0); }
STATIC EFI_STATUS CollectBootOrder (IN CONST CHAR16 *Root) { return BdtBootOrder (); }
STATIC EFI_STATUS CollectBootOptions (IN CONST CHAR16 *Root) { return BdtBootOptions (); }
STATIC EFI_STATUS CollectBootTiming (IN CONST CHAR16 *Root) { return BdtBootTiming (); }
STATIC EFI_STATUS CollectBootModules (IN CONST CHAR16 *Root) { return BdtBootModules (); }
STATIC EFI_STATUS CollectPciList (IN CONST CHAR16 *Root) { return BdtPciList (); }
STATIC EFI_STATUS CollectPciTree (IN CONST CHAR16 *Root) { return BdtPciTree (); }
STATIC EFI_STATUS CollectPciAer (IN CONST CHAR16 *Root) { return BdtPciAer (NULL); }
STATIC EFI_STATUS CollectAcpiList (IN CONST CHAR16 *Root) { return BdtAcpiList (); }
STATIC EFI_STATUS CollectAcpiValidate (IN CONST CHAR16 *Root) { return BdtAcpiValidate (); }
STATIC EFI_STATUS CollectVarList (IN CONST CHAR16 *Root) { return BdtVarList (); }
STATIC EFI_STATUS CollectVarUsage (IN CONST CHAR16 *Root) { return BdtVarUsage (); }
STATIC EFI_STATUS CollectHandles (IN CONST CHAR16 *Root) { return BdtHandleList (); }
STATIC EFI_STATUS CollectHandleTree (IN CONST CHAR16 *Root) { return BdtHandleTree (); }
STATIC EFI_STATUS CollectProtocols (IN CONST CHAR16 *Root) { return BdtProtocolList (); }
STATIC EFI_STATUS CollectDevicePaths (IN CONST CHAR16 *Root) { return BdtDevicePathList (); }
STATIC EFI_STATUS CollectCpuid (IN CONST CHAR16 *Root) { return BdtCpuCpuid (TRUE, 0, 0); }
STATIC EFI_STATUS CollectMtrr (IN CONST CHAR16 *Root) { return BdtCpuMtrr (); }
STATIC EFI_STATUS CollectMca (IN CONST CHAR16 *Root) { return BdtCpuMca (); }
STATIC EFI_STATUS CollectSecureBoot (IN CONST CHAR16 *Root) { return BdtSecureBoot (); }
STATIC EFI_STATUS CollectKek (IN CONST CHAR16 *Root) { return BdtSecurityDb (L"KEK"); }
STATIC EFI_STATUS CollectDb (IN CONST CHAR16 *Root) { return BdtSecurityDb (L"db"); }
STATIC EFI_STATUS CollectDbx (IN CONST CHAR16 *Root) { return BdtSecurityDb (L"dbx"); }
STATIC EFI_STATUS CollectTpm (IN CONST CHAR16 *Root) { return BdtTpmInfo (); }
STATIC EFI_STATUS CollectPcr (IN CONST CHAR16 *Root) { return BdtTpmPcr (MAX_UINT32); }
STATIC EFI_STATUS CollectTcgLog (IN CONST CHAR16 *Root) { return BdtTcgLog (); }

STATIC
EFI_STATUS
CollectAcpiBinaries (
  IN CONST CHAR16  *Root
  )
{
  UnicodeSPrint (mBinaryPath, sizeof (mBinaryPath), L"%s\\acpi\\tables", Root);
  return BdtAcpiSaveTables (mBinaryPath);
}

STATIC
EFI_STATUS
CollectVarBinaries (
  IN CONST CHAR16  *Root
  )
{
  UnicodeSPrint (mBinaryPath, sizeof (mBinaryPath), L"%s\\uefi\\variables", Root);
  return BdtVarSaveAll (mBinaryPath);
}

STATIC CONST BDT_COLLECT_ITEM  mItems[] = {
  { COLLECT_QUICK,  L"system\\summary",          L"system",              CollectSystem         },
  { COLLECT_QUICK,  L"system\\cpu",              L"cpu info",            CollectCpuInfo        },
  { COLLECT_QUICK,  L"system\\memory",           L"memory info",         CollectMemoryInfo     },
  { COLLECT_NORMAL, L"system\\memory_map",       L"memory map",          CollectMemoryMap      },
  { COLLECT_QUICK,  L"system\\smbios",           L"smbios dump",         CollectSmbios         },
  { COLLECT_NORMAL, L"system\\smbios_validate",  L"smbios validate",     CollectSmbiosValidate },
  { COLLECT_NORMAL, L"system\\firmware",         L"firmware fv",         CollectFirmwareFv     },
  { COLLECT_FULL,   L"system\\ffs",              L"firmware ffs",        CollectFirmwareFfs    },
  { COLLECT_QUICK,  L"boot\\bootorder",          L"boot order",          CollectBootOrder      },
  { COLLECT_NORMAL, L"boot\\bootoptions",        L"boot option",         CollectBootOptions    },
  { COLLECT_NORMAL, L"boot\\timing",             L"boot timing",         CollectBootTiming     },
  { COLLECT_NORMAL, L"boot\\modules",            L"boot modules",        CollectBootModules    },
  { COLLECT_QUICK,  L"pci\\devices",             L"pci list",            CollectPciList        },
  { COLLECT_QUICK,  L"pci\\tree",                L"pci tree",            CollectPciTree        },
  { COLLECT_NORMAL, L"pci\\aer",                 L"pci aer",             CollectPciAer         },
  { COLLECT_QUICK,  L"acpi\\tables",             L"acpi list",           CollectAcpiList       },
  { COLLECT_NORMAL, L"acpi\\validate",           L"acpi validate",       CollectAcpiValidate   },
  { COLLECT_NORMAL, L"acpi\\binaries",           L"acpi save",           CollectAcpiBinaries   },
  { COLLECT_NORMAL, L"uefi\\variables",          L"var list",            CollectVarList        },
  { COLLECT_NORMAL, L"uefi\\variable_usage",     L"var usage",           CollectVarUsage       },
  { COLLECT_FULL,   L"uefi\\variable_binaries",  L"var save",            CollectVarBinaries    },
  { COLLECT_NORMAL, L"uefi\\handles",            L"handle list",         CollectHandles        },
  { COLLECT_NORMAL, L"uefi\\protocols",          L"protocol list",       CollectProtocols      },
  { COLLECT_FULL,   L"uefi\\handle_tree",        L"handle tree",         CollectHandleTree     },
  { COLLECT_FULL,   L"uefi\\devicepaths",        L"devicepath list",     CollectDevicePaths    },
  { COLLECT_NORMAL, L"cpu\\cpuid",               L"cpu cpuid",           CollectCpuid          },
  { COLLECT_NORMAL, L"cpu\\mtrr",                L"cpu mtrr",            CollectMtrr           },
  { COLLECT_NORMAL, L"cpu\\mca",                 L"cpu mca",             CollectMca            },
  { COLLECT_QUICK,  L"security\\secureboot",     L"security secureboot", CollectSecureBoot     },
  { COLLECT_FULL,   L"security\\kek",            L"security kek",        CollectKek            },
  { COLLECT_FULL,   L"security\\db",             L"security db",         CollectDb             },
  { COLLECT_FULL,   L"security\\dbx",            L"security dbx",        CollectDbx            },
  { COLLECT_QUICK,  L"security\\tpm",            L"security tpm",        CollectTpm            },
  { COLLECT_NORMAL, L"security\\pcr",            L"security pcr",        CollectPcr            },
  { COLLECT_NORMAL, L"security\\tcglog",         L"security tcglog",     CollectTcgLog         },
};

STATIC CONST CHAR16  *mDirectories[] = {
  L"system", L"boot", L"pci", L"acpi", L"uefi", L"cpu", L"security"
};

/**
  Run one item into its own file. Returns the item status.
**/
STATIC
EFI_STATUS
RunItem (
  IN CONST CHAR16            *Root,
  IN CONST BDT_COLLECT_ITEM  *Item,
  OUT CHAR16                 *FileName,
  IN UINTN                   FileNameSize
  )
{
  EFI_STATUS         Status;
  SHELL_FILE_HANDLE  File;
  CHAR16             Path[512];

  UnicodeSPrint (FileName, FileNameSize, L"%s.%s", Item->Path, BdtOutGetExtension ());
  UnicodeSPrint (Path, sizeof (Path), L"%s\\%s", Root, FileName);
  Status = BdtFileCreate (Path, &File);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  BdtOutSetFile (File);
  BdtOutBegin (Item->Command);
  Status = Item->Collect (Root);
  BdtOutEnd ();
  BdtOutSetFile (NULL);
  BdtFileClose (File);
  return Status;
}

EFI_STATUS
BdtCmdCollect (
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"file",    L"File",    36 },
    { L"command", L"Command", 22 },
    { L"status",  L"Status",  0  },
  };
  EFI_STATUS         Status;
  EFI_STATUS         ItemStatus;
  UINT8              Level;
  UINTN              Index;
  UINTN              Failed;
  EFI_TIME           Time;
  CHAR16             Root[384];
  CHAR16             Path[512];
  CHAR16             FileName[64];
  EFI_STATUS         *Results;
  BDT_OUTPUT_FORMAT  Format;
  CONST CHAR16       *Extension;
  SHELL_FILE_HANDLE  Manifest;

  Level = COLLECT_NORMAL;
  for (Index = 0; Index < Argc; Index++) {
    if (BdtStriCmp (Argv[Index], L"--quick") == 0) {
      Level = COLLECT_QUICK;
    } else if (BdtStriCmp (Argv[Index], L"--full") == 0) {
      Level = COLLECT_FULL;
    } else {
      return EFI_INVALID_PARAMETER;
    }
  }

  ZeroMem (&Time, sizeof (Time));
  gRT->GetTime (&Time, NULL);
  if ((Context->OutputPath != NULL) && (*Context->OutputPath != L'\0')) {
    Status = BdtFileCreateDirectory (Context->OutputPath);
    if (EFI_ERROR (Status)) {
      BdtOutError (L"Cannot create %s (%s)", Context->OutputPath, BdtStatusName (Status));
      return Status;
    }

    UnicodeSPrint (
      Root,
      sizeof (Root),
      L"%s\\BDT_%04d%02d%02d_%02d%02d%02d",
      Context->OutputPath,
      Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second
      );
  } else {
    UnicodeSPrint (
      Root,
      sizeof (Root),
      L"BDT_%04d%02d%02d_%02d%02d%02d",
      Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second
      );
  }

  Status = BdtFileCreateDirectory (Root);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"Cannot create %s (%s). Change to a writable file system first, e.g. \"fs0:\".", Root, BdtStatusName (Status));
    return Status;
  }

  for (Index = 0; Index < ARRAY_SIZE (mDirectories); Index++) {
    UnicodeSPrint (Path, sizeof (Path), L"%s\\%s", Root, mDirectories[Index]);
    BdtFileCreateDirectory (Path);
  }

  Results = AllocateZeroPool (ARRAY_SIZE (mItems) * sizeof (EFI_STATUS));
  if (Results == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ShellPrintEx (
    -1,
    -1,
    L"BDT collect (%s) -> %s\r\n",
    (Level == COLLECT_QUICK) ? L"quick" : ((Level == COLLECT_FULL) ? L"full" : L"normal"),
    Root
    );

  Failed = 0;
  for (Index = 0; Index < ARRAY_SIZE (mItems); Index++) {
    if (mItems[Index].Level > Level) {
      Results[Index] = EFI_NOT_STARTED;
      continue;
    }

    ItemStatus     = RunItem (Root, &mItems[Index], FileName, sizeof (FileName));
    Results[Index] = ItemStatus;
    if (EFI_ERROR (ItemStatus)) {
      Failed++;
    }

    ShellPrintEx (-1, -1, L"  [%s] %s\r\n", EFI_ERROR (ItemStatus) ? L"WARN" : L" OK ", FileName);
  }

  //
  // The manifest is always plain text.
  //
  Format    = BdtOutGetFormat ();
  Extension = BdtOutGetExtension ();
  BdtOutSetFormat (BdtFormatText);
  UnicodeSPrint (Path, sizeof (Path), L"%s\\manifest.txt", Root);
  Status = BdtFileCreate (Path, &Manifest);
  if (!EFI_ERROR (Status)) {
    BdtOutSetFile (Manifest);
    BdtOutBegin (L"collect");
    BdtOutSectionBegin (L"BDT Debug Package");
    BdtOutField (L"Tool", L"%s %s", BDT_TOOL_NAME, BDT_VERSION_STRING);
    BdtOutField (L"Date", L"%04d-%02d-%02d %02d:%02d:%02d", Time.Year, Time.Month, Time.Day, Time.Hour, Time.Minute, Time.Second);
    BdtOutField (L"Mode", L"%s", (Level == COLLECT_QUICK) ? L"quick" : ((Level == COLLECT_FULL) ? L"full" : L"normal"));
    BdtOutField (
      L"Format",
      L"%s",
      (Format == BdtFormatJson) ? L"json" : ((Format == BdtFormatCsv) ? L"csv" : L"text")
      );
    BdtOutField (L"Firmware Vendor", L"%s", (gST->FirmwareVendor != NULL) ? gST->FirmwareVendor : L"");
    BdtOutFieldHex (L"Firmware Revision", gST->FirmwareRevision, 8);
    BdtOutFieldNum (L"Items Failed", Failed);
    BdtOutSectionEnd ();

    BdtOutTableBegin (L"files", Columns, ARRAY_SIZE (Columns));
    for (Index = 0; Index < ARRAY_SIZE (mItems); Index++) {
      if (Results[Index] == EFI_NOT_STARTED) {
        continue;
      }

      UnicodeSPrint (FileName, sizeof (FileName), L"%s.%s", mItems[Index].Path, Extension);
      BdtOutRowBegin ();
      BdtOutCell (L"%s", FileName);
      BdtOutCell (L"%s", mItems[Index].Command);
      BdtOutCell (L"%s", EFI_ERROR (Results[Index]) ? BdtStatusName (Results[Index]) : L"OK");
      BdtOutRowEnd ();
    }

    BdtOutTableEnd ();
    BdtOutEnd ();
    BdtOutSetFile (NULL);
    BdtFileClose (Manifest);
  }

  BdtOutSetFormat (Format);
  FreePool (Results);
  ShellPrintEx (-1, -1, L"Done: %d item(s) with warnings. Package: %s\r\n", Failed, Root);
  return EFI_SUCCESS;
}
