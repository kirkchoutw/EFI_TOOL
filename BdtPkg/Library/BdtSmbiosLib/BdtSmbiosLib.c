/** @file
  BdtSmbiosLib - SMBIOS inspection.

  The SMBIOS table is read through the EFI configuration table (SMBIOS 3.x
  entry point preferred, 2.x as fallback), so no SMBIOS protocol is needed.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <BdtCommon.h>
#include <Guid/SmBios.h>
#include <IndustryStandard/SmBios.h>
#include <Library/BdtSmbiosLib.h>

#define BDT_SMBIOS_MAX_STRUCTURES  4096
#define SMBIOS_TYPE_END            127

typedef struct {
  UINT8     *Start;
  UINT8     *End;
  UINT8     Major;
  UINT8     Minor;
  UINT8     DocRev;
  BOOLEAN   Is3x;
  VOID      *EntryPoint;
  UINTN     EntryPointLength;
  UINTN     DeclaredCount;      ///< 2.x only, 0 for 3.x
} BDT_SMBIOS_TABLE;

typedef struct {
  UINT8           Type;
  CONST CHAR16    *Name;
} BDT_SMBIOS_TYPE_NAME;

STATIC CONST BDT_SMBIOS_TYPE_NAME  mTypeNames[] = {
  { 0,   L"BIOS Information"                     },
  { 1,   L"System Information"                   },
  { 2,   L"Baseboard Information"                },
  { 3,   L"System Enclosure"                     },
  { 4,   L"Processor Information"                },
  { 7,   L"Cache Information"                    },
  { 8,   L"Port Connector Information"           },
  { 9,   L"System Slots"                         },
  { 10,  L"On Board Devices Information"         },
  { 11,  L"OEM Strings"                          },
  { 12,  L"System Configuration Options"         },
  { 13,  L"BIOS Language Information"            },
  { 14,  L"Group Associations"                   },
  { 15,  L"System Event Log"                     },
  { 16,  L"Physical Memory Array"                },
  { 17,  L"Memory Device"                        },
  { 18,  L"32-bit Memory Error Information"      },
  { 19,  L"Memory Array Mapped Address"          },
  { 20,  L"Memory Device Mapped Address"         },
  { 21,  L"Built-in Pointing Device"             },
  { 22,  L"Portable Battery"                     },
  { 23,  L"System Reset"                         },
  { 24,  L"Hardware Security"                    },
  { 25,  L"System Power Controls"                },
  { 26,  L"Voltage Probe"                        },
  { 27,  L"Cooling Device"                       },
  { 28,  L"Temperature Probe"                    },
  { 29,  L"Electrical Current Probe"             },
  { 30,  L"Out-of-Band Remote Access"            },
  { 31,  L"Boot Integrity Services Entry Point"  },
  { 32,  L"System Boot Information"              },
  { 33,  L"64-bit Memory Error Information"      },
  { 34,  L"Management Device"                    },
  { 35,  L"Management Device Component"          },
  { 36,  L"Management Device Threshold Data"     },
  { 37,  L"Memory Channel"                       },
  { 38,  L"IPMI Device Information"              },
  { 39,  L"System Power Supply"                  },
  { 40,  L"Additional Information"               },
  { 41,  L"Onboard Devices Extended Information" },
  { 42,  L"Management Controller Host Interface" },
  { 43,  L"TPM Device"                           },
  { 44,  L"Processor Additional Information"     },
  { 45,  L"Firmware Inventory Information"       },
  { 46,  L"String Property"                      },
  { 126, L"Inactive"                             },
  { 127, L"End-of-Table"                         },
};

STATIC CONST CHAR16  *mMemoryTypes[] = {
  L"Unknown", L"Other", L"Unknown", L"DRAM", L"EDRAM", L"VRAM", L"SRAM", L"RAM",
  L"ROM", L"FLASH", L"EEPROM", L"FEPROM", L"EPROM", L"CDRAM", L"3DRAM", L"SDRAM",
  L"SGRAM", L"RDRAM", L"DDR", L"DDR2", L"DDR2 FB-DIMM", L"Reserved", L"Reserved", L"Reserved",
  L"DDR3", L"FBD2", L"DDR4", L"LPDDR", L"LPDDR2", L"LPDDR3", L"LPDDR4", L"Logical non-volatile",
  L"HBM", L"HBM2", L"DDR5", L"LPDDR5", L"HBM3"
};

STATIC
CONST CHAR16 *
TypeName (
  IN UINT8  Type
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mTypeNames); Index++) {
    if (mTypeNames[Index].Type == Type) {
      return mTypeNames[Index].Name;
    }
  }

  return (Type >= 128) ? L"OEM Specific" : L"Unknown";
}

STATIC
EFI_STATUS
GetTable (
  OUT BDT_SMBIOS_TABLE  *Table
  )
{
  SMBIOS_TABLE_3_0_ENTRY_POINT  *Entry3;
  SMBIOS_TABLE_ENTRY_POINT      *Entry2;

  ZeroMem (Table, sizeof (*Table));
  if (!EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiSmbios3TableGuid, (VOID **)&Entry3)) && (Entry3 != NULL) &&
      (Entry3->TableAddress != 0) && (Entry3->TableAddress <= MAX_ADDRESS))
  {
    Table->Start            = (UINT8 *)(UINTN)Entry3->TableAddress;
    Table->End              = Table->Start + Entry3->TableMaximumSize;
    Table->Major            = Entry3->MajorVersion;
    Table->Minor            = Entry3->MinorVersion;
    Table->DocRev           = Entry3->DocRev;
    Table->Is3x             = TRUE;
    Table->EntryPoint       = Entry3;
    Table->EntryPointLength = Entry3->EntryPointLength;
    return EFI_SUCCESS;
  }

  if (!EFI_ERROR (EfiGetSystemConfigurationTable (&gEfiSmbiosTableGuid, (VOID **)&Entry2)) && (Entry2 != NULL) &&
      (Entry2->TableAddress != 0))
  {
    Table->Start            = (UINT8 *)(UINTN)Entry2->TableAddress;
    Table->End              = Table->Start + Entry2->TableLength;
    Table->Major            = Entry2->MajorVersion;
    Table->Minor            = Entry2->MinorVersion;
    Table->Is3x             = FALSE;
    Table->EntryPoint       = Entry2;
    Table->EntryPointLength = Entry2->EntryPointLength;
    Table->DeclaredCount    = Entry2->NumberOfSmbiosStructures;
    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

/**
  Return the structure after Current, or NULL at end of table / on corruption.
  Pass Current = NULL to get the first structure.
**/
STATIC
SMBIOS_STRUCTURE *
NextStructure (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *Current OPTIONAL
  )
{
  UINT8  *Walk;

  if (Current == NULL) {
    Walk = Table->Start;
  } else {
    if (Current->Type == SMBIOS_TYPE_END) {
      return NULL;
    }

    Walk = (UINT8 *)Current + Current->Length;
    while (Walk + 1 < Table->End && (Walk[0] != 0 || Walk[1] != 0)) {
      Walk++;
    }

    Walk += 2;
  }

  if ((Walk + sizeof (SMBIOS_STRUCTURE) > Table->End) ||
      (((SMBIOS_STRUCTURE *)Walk)->Length < sizeof (SMBIOS_STRUCTURE)) ||
      (Walk + ((SMBIOS_STRUCTURE *)Walk)->Length > Table->End))
  {
    return NULL;
  }

  return (SMBIOS_STRUCTURE *)Walk;
}

/**
  Return string number Index (1-based) of a structure, or "" when absent.
**/
STATIC
CONST CHAR8 *
GetString (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *Structure,
  IN UINT8             Index
  )
{
  CONST CHAR8  *Walk;
  UINT8        Number;

  if (Index == 0) {
    return "";
  }

  Walk = (CONST CHAR8 *)Structure + Structure->Length;
  for (Number = 1; (UINT8 *)Walk < Table->End && *Walk != '\0'; Number++) {
    if (Number == Index) {
      return Walk;
    }

    while ((UINT8 *)Walk < Table->End && *Walk != '\0') {
      Walk++;
    }

    Walk++;
  }

  return "";
}

STATIC
SMBIOS_STRUCTURE *
FindType (
  IN BDT_SMBIOS_TABLE  *Table,
  IN UINT8             Type
  )
{
  SMBIOS_STRUCTURE  *Structure;
  UINTN             Count;

  Structure = NULL;
  for (Count = 0; Count < BDT_SMBIOS_MAX_STRUCTURES; Count++) {
    Structure = NextStructure (Table, Structure);
    if ((Structure == NULL) || (Structure->Type == Type)) {
      return Structure;
    }
  }

  return NULL;
}

//
// Field accessors with bounds check against the formatted area length.
//
#define HAS(S, Off, Size)  ((S)->Length >= (Off) + (Size))
#define B(S, Off)          (*((UINT8 *)(S) + (Off)))
#define W(S, Off)          ReadUnaligned16 ((UINT16 *)((UINT8 *)(S) + (Off)))
#define D(S, Off)          ReadUnaligned32 ((UINT32 *)((UINT8 *)(S) + (Off)))
#define Q(S, Off)          ReadUnaligned64 ((UINT64 *)((UINT8 *)(S) + (Off)))

STATIC
VOID
OutString (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S,
  IN CONST CHAR16      *Key,
  IN UINTN             Offset
  )
{
  if (HAS (S, Offset, 1)) {
    BdtOutField (Key, L"%a", GetString (Table, S, B (S, Offset)));
  }
}

STATIC
VOID
DecodeBios (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  UINT32  RomSize;

  OutString (Table, S, L"Vendor", 0x04);
  OutString (Table, S, L"Version", 0x05);
  if (HAS (S, 0x06, 2)) {
    BdtOutFieldHex (L"Starting Segment", W (S, 0x06), 4);
  }

  OutString (Table, S, L"Release Date", 0x08);
  if (HAS (S, 0x09, 1)) {
    RomSize = (B (S, 0x09) + 1) * 64;
    if ((B (S, 0x09) == 0xFF) && HAS (S, 0x18, 2)) {
      RomSize = (W (S, 0x18) & 0x3FFF) * (((W (S, 0x18) >> 14) == 0) ? 1024 : 1024 * 1024);
    }

    BdtOutField (L"ROM Size", L"%d KB", RomSize);
  }

  if (HAS (S, 0x0A, 8)) {
    BdtOutFieldHex (L"Characteristics", Q (S, 0x0A), 16);
  }

  if (HAS (S, 0x15, 1)) {
    BdtOutField (L"BIOS Release", L"%d.%d", B (S, 0x14), B (S, 0x15));
  }

  if (HAS (S, 0x17, 1)) {
    BdtOutField (L"EC Firmware Release", L"%d.%d", B (S, 0x16), B (S, 0x17));
  }
}

STATIC
VOID
DecodeSystem (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  OutString (Table, S, L"Manufacturer", 0x04);
  OutString (Table, S, L"Product Name", 0x05);
  OutString (Table, S, L"Version", 0x06);
  OutString (Table, S, L"Serial Number", 0x07);
  if (HAS (S, 0x08, 16)) {
    BdtOutField (L"UUID", L"%g", (EFI_GUID *)((UINT8 *)S + 0x08));
  }

  if (HAS (S, 0x18, 1)) {
    BdtOutFieldNum (L"Wake-up Type", B (S, 0x18));
  }

  OutString (Table, S, L"SKU Number", 0x19);
  OutString (Table, S, L"Family", 0x1A);
}

STATIC
VOID
DecodeBoard (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  OutString (Table, S, L"Manufacturer", 0x04);
  OutString (Table, S, L"Product", 0x05);
  OutString (Table, S, L"Version", 0x06);
  OutString (Table, S, L"Serial Number", 0x07);
  OutString (Table, S, L"Asset Tag", 0x08);
  if (S->Type == 3) {
    if (HAS (S, 0x05, 1)) {
      BdtOutFieldHex (L"Chassis Type", B (S, 0x05) & 0x7F, 2);
    }
  }
}

STATIC
VOID
DecodeProcessor (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  OutString (Table, S, L"Socket Designation", 0x04);
  if (HAS (S, 0x06, 1)) {
    BdtOutFieldHex (L"Processor Family", B (S, 0x06), 2);
  }

  OutString (Table, S, L"Manufacturer", 0x07);
  if (HAS (S, 0x08, 8)) {
    BdtOutFieldHex (L"Processor ID", Q (S, 0x08), 16);
  }

  OutString (Table, S, L"Version", 0x10);
  if (HAS (S, 0x12, 2)) {
    BdtOutField (L"External Clock", L"%d MHz", W (S, 0x12));
  }

  if (HAS (S, 0x14, 2)) {
    BdtOutField (L"Max Speed", L"%d MHz", W (S, 0x14));
  }

  if (HAS (S, 0x16, 2)) {
    BdtOutField (L"Current Speed", L"%d MHz", W (S, 0x16));
  }

  if (HAS (S, 0x18, 1)) {
    BdtOutFieldBool (L"Socket Populated", (B (S, 0x18) & BIT6) != 0);
    BdtOutFieldNum (L"CPU Status", B (S, 0x18) & 0x7);
  }

  OutString (Table, S, L"Serial Number", 0x20);
  OutString (Table, S, L"Asset Tag", 0x21);
  OutString (Table, S, L"Part Number", 0x22);
  if (HAS (S, 0x25, 1)) {
    BdtOutFieldNum (L"Core Count", ((B (S, 0x23) == 0xFF) && HAS (S, 0x2A, 2)) ? W (S, 0x2A) : B (S, 0x23));
    BdtOutFieldNum (L"Core Enabled", ((B (S, 0x24) == 0xFF) && HAS (S, 0x2C, 2)) ? W (S, 0x2C) : B (S, 0x24));
    BdtOutFieldNum (L"Thread Count", ((B (S, 0x25) == 0xFF) && HAS (S, 0x2E, 2)) ? W (S, 0x2E) : B (S, 0x25));
  }
}

STATIC
VOID
DecodeCache (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  UINT16  Raw;

  OutString (Table, S, L"Socket Designation", 0x04);
  if (HAS (S, 0x05, 2)) {
    BdtOutFieldNum (L"Level", (W (S, 0x05) & 0x7) + 1);
    BdtOutFieldBool (L"Enabled", (W (S, 0x05) & BIT7) != 0);
  }

  if (HAS (S, 0x09, 2)) {
    Raw = W (S, 0x09);
    BdtOutField (L"Installed Size", L"%d KB", (Raw & 0x7FFF) * (((Raw & BIT15) != 0) ? 64 : 1));
  }
}

STATIC
VOID
DecodeSlot (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  OutString (Table, S, L"Designation", 0x04);
  if (HAS (S, 0x08, 1)) {
    BdtOutFieldHex (L"Slot Type", B (S, 0x05), 2);
    BdtOutFieldHex (L"Data Bus Width", B (S, 0x06), 2);
    BdtOutField (L"Current Usage", L"%s", (B (S, 0x07) == 3) ? L"In Use" : ((B (S, 0x07) == 4) ? L"Available" : L"Other"));
  }

  if (HAS (S, 0x10, 1)) {
    BdtOutField (L"Bus Address", L"%04x:%02x:%02x.%x", W (S, 0x0D), B (S, 0x0F), B (S, 0x10) >> 3, B (S, 0x10) & 0x7);
  }
}

STATIC
VOID
DecodeMemoryArray (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  UINT64  CapacityKb;

  if (HAS (S, 0x0F, 0)) {
    BdtOutFieldHex (L"Location", B (S, 0x04), 2);
    BdtOutFieldHex (L"Use", B (S, 0x05), 2);
    BdtOutFieldHex (L"Error Correction", B (S, 0x06), 2);
    CapacityKb = D (S, 0x07);
    if ((CapacityKb == 0x80000000) && HAS (S, 0x0F, 8)) {
      CapacityKb = DivU64x32 (Q (S, 0x0F), 1024);
    }

    BdtOutField (L"Maximum Capacity", L"%lu MB", DivU64x32 (CapacityKb, 1024));
    BdtOutFieldNum (L"Number of Devices", W (S, 0x0D));
  }
}

STATIC
VOID
DecodeMemoryDevice (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  UINT16  Size;
  UINT64  SizeMb;
  UINT32  Speed;

  if (!HAS (S, 0x15, 0)) {
    return;
  }

  OutString (Table, S, L"Locator", 0x10);
  OutString (Table, S, L"Bank Locator", 0x11);
  Size = W (S, 0x0C);
  if (Size == 0) {
    BdtOutField (L"Size", L"No Module Installed");
  } else if (Size == 0xFFFF) {
    BdtOutField (L"Size", L"Unknown");
  } else {
    if ((Size == 0x7FFF) && HAS (S, 0x1C, 4)) {
      SizeMb = D (S, 0x1C) & 0x7FFFFFFF;
    } else if ((Size & BIT15) != 0) {
      SizeMb = (Size & 0x7FFF) / 1024;
    } else {
      SizeMb = Size;
    }

    BdtOutField (L"Size", L"%lu MB", SizeMb);
  }

  BdtOutField (L"Memory Type", L"%s", (B (S, 0x12) < ARRAY_SIZE (mMemoryTypes)) ? mMemoryTypes[B (S, 0x12)] : L"Unknown");
  if (HAS (S, 0x15, 2)) {
    Speed = W (S, 0x15);
    if ((Speed == 0xFFFF) && HAS (S, 0x54, 4)) {
      Speed = D (S, 0x54);
    }

    BdtOutField (L"Speed", L"%d MT/s", Speed);
  }

  OutString (Table, S, L"Manufacturer", 0x17);
  OutString (Table, S, L"Serial Number", 0x18);
  OutString (Table, S, L"Asset Tag", 0x19);
  OutString (Table, S, L"Part Number", 0x1A);
  if (HAS (S, 0x1B, 1)) {
    BdtOutFieldNum (L"Rank", B (S, 0x1B) & 0xF);
  }

  if (HAS (S, 0x20, 2)) {
    Speed = W (S, 0x20);
    if ((Speed == 0xFFFF) && HAS (S, 0x58, 4)) {
      Speed = D (S, 0x58);
    }

    BdtOutField (L"Configured Speed", L"%d MT/s", Speed);
  }

  if (HAS (S, 0x08, 4)) {
    BdtOutField (L"Total / Data Width", L"%d / %d bits", W (S, 0x08), W (S, 0x0A));
  }
}

STATIC
VOID
DecodeMappedAddress (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  UINT64  Start;
  UINT64  End;

  if (!HAS (S, 0x0F, 0)) {
    return;
  }

  Start = LShiftU64 (D (S, 0x04), 10);
  End   = LShiftU64 (D (S, 0x08), 10) + 0x3FF;
  if ((D (S, 0x04) == 0xFFFFFFFF) && HAS (S, 0x17, 8)) {
    Start = Q (S, 0x0F);
    End   = Q (S, 0x17);
  }

  BdtOutFieldHex (L"Starting Address", Start, 16);
  BdtOutFieldHex (L"Ending Address", End, 16);
  BdtOutField (L"Range Size", L"%lu MB", RShiftU64 (End - Start + 1, 20));
  BdtOutFieldHex (L"Array Handle", W (S, 0x0C), 4);
  BdtOutFieldNum (L"Partition Width", B (S, 0x0E));
}

STATIC
VOID
DecodeBoot (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  if (HAS (S, 0x0A, 1)) {
    BdtOutFieldNum (L"Boot Status", B (S, 0x0A));
    BdtOutField (L"Meaning", L"%s", (B (S, 0x0A) == 0) ? L"No errors detected" : L"Error / event reported");
  }
}

STATIC
VOID
DecodeStructure (
  IN BDT_SMBIOS_TABLE  *Table,
  IN SMBIOS_STRUCTURE  *S
  )
{
  CONST CHAR8  *Walk;
  UINTN        Number;

  BdtOutItemBegin (L"Type %d - %s (Handle 0x%04x)", S->Type, TypeName (S->Type), S->Handle);
  if (BdtOutGetFormat () != BdtFormatText) {
    BdtOutFieldNum (L"Type", S->Type);
    BdtOutField (L"Name", L"%s", TypeName (S->Type));
    BdtOutFieldHex (L"Handle", S->Handle, 4);
  }

  BdtOutFieldHex (L"Length", S->Length, 2);

  switch (S->Type) {
    case 0:
      DecodeBios (Table, S);
      break;
    case 1:
      DecodeSystem (Table, S);
      break;
    case 2:
    case 3:
      DecodeBoard (Table, S);
      break;
    case 4:
      DecodeProcessor (Table, S);
      break;
    case 7:
      DecodeCache (Table, S);
      break;
    case 9:
      DecodeSlot (Table, S);
      break;
    case 16:
      DecodeMemoryArray (Table, S);
      break;
    case 17:
      DecodeMemoryDevice (Table, S);
      break;
    case 19:
      DecodeMappedAddress (Table, S);
      break;
    case 32:
      DecodeBoot (Table, S);
      break;
    default:
      BdtOutHexDump (L"Formatted Area", 0, S, S->Length);
      break;
  }

  //
  // Raw string set; useful for OEM types and for spotting bad strings.
  //
  Walk = (CONST CHAR8 *)S + S->Length;
  if (*Walk != '\0') {
    BdtOutListBegin (L"Strings");
    for (Number = 1; (UINT8 *)Walk < Table->End && *Walk != '\0'; Number++) {
      BdtOutItemBegin (L"%d: %a", Number, Walk);
      if (BdtOutGetFormat () != BdtFormatText) {
        BdtOutFieldNum (L"index", Number);
        BdtOutField (L"value", L"%a", Walk);
      }

      BdtOutItemEnd ();
      while ((UINT8 *)Walk < Table->End && *Walk != '\0') {
        Walk++;
      }

      Walk++;
    }

    BdtOutListEnd ();
  }

  BdtOutItemEnd ();
}

EFI_STATUS
EFIAPI
BdtSmbiosGetVersion (
  OUT UINT8  *Major,
  OUT UINT8  *Minor,
  OUT UINT8  *DocRev
  )
{
  BDT_SMBIOS_TABLE  Table;
  EFI_STATUS        Status;

  Status = GetTable (&Table);
  if (!EFI_ERROR (Status)) {
    *Major  = Table.Major;
    *Minor  = Table.Minor;
    *DocRev = Table.DocRev;
  }

  return Status;
}

EFI_STATUS
EFIAPI
BdtSmbiosGetBiosInfo (
  OUT CHAR16  *Vendor,
  IN  UINTN   VendorSize,
  OUT CHAR16  *Version,
  IN  UINTN   VersionSize,
  OUT CHAR16  *Date,
  IN  UINTN   DateSize
  )
{
  BDT_SMBIOS_TABLE  Table;
  SMBIOS_STRUCTURE  *S;
  EFI_STATUS        Status;

  Vendor[0]  = L'\0';
  Version[0] = L'\0';
  Date[0]    = L'\0';
  Status     = GetTable (&Table);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  S = FindType (&Table, 0);
  if ((S == NULL) || !HAS (S, 0x09, 0)) {
    return EFI_NOT_FOUND;
  }

  UnicodeSPrint (Vendor, VendorSize, L"%a", GetString (&Table, S, B (S, 0x04)));
  UnicodeSPrint (Version, VersionSize, L"%a", GetString (&Table, S, B (S, 0x05)));
  UnicodeSPrint (Date, DateSize, L"%a", GetString (&Table, S, B (S, 0x08)));
  return EFI_SUCCESS;
}

STATIC
VOID
OutEntryPoint (
  IN BDT_SMBIOS_TABLE  *Table
  )
{
  BdtOutSectionBegin (L"Entry Point");
  BdtOutField (L"Version", L"%d.%d.%d", Table->Major, Table->Minor, Table->DocRev);
  BdtOutField (L"Entry Point Type", L"%s", Table->Is3x ? L"SMBIOS 3.x (64-bit)" : L"SMBIOS 2.x (32-bit)");
  BdtOutFieldHex (L"Entry Point Address", (UINTN)Table->EntryPoint, 0);
  BdtOutFieldHex (L"Table Address", (UINTN)Table->Start, 0);
  BdtOutFieldHex (L"Table Length", Table->End - Table->Start, 0);
  if (!Table->Is3x) {
    BdtOutFieldNum (L"Structure Count", Table->DeclaredCount);
  }

  BdtOutSectionEnd ();
}

EFI_STATUS
EFIAPI
BdtSmbiosList (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"handle", L"Handle", 8  },
    { L"type",   L"Type",   6  },
    { L"length", L"Length", 8  },
    { L"name",   L"Name",   0  },
  };
  BDT_SMBIOS_TABLE  Table;
  SMBIOS_STRUCTURE  *S;
  EFI_STATUS        Status;
  UINTN             Count;

  Status = GetTable (&Table);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"SMBIOS table not found");
    return Status;
  }

  OutEntryPoint (&Table);
  BdtOutTableBegin (L"structures", Columns, ARRAY_SIZE (Columns));
  S = NULL;
  for (Count = 0; Count < BDT_SMBIOS_MAX_STRUCTURES; Count++) {
    S = NextStructure (&Table, S);
    if (S == NULL) {
      break;
    }

    BdtOutRowBegin ();
    BdtOutCell (L"0x%04x", S->Handle);
    BdtOutCell (L"%d", S->Type);
    BdtOutCell (L"0x%02x", S->Length);
    BdtOutCell (L"%s", TypeName (S->Type));
    BdtOutRowEnd ();
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Structure Count", Count);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
BdtSmbiosDump (
  IN UINT16  Type
  )
{
  BDT_SMBIOS_TABLE  Table;
  SMBIOS_STRUCTURE  *S;
  EFI_STATUS        Status;
  UINTN             Count;
  UINTN             Shown;

  Status = GetTable (&Table);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"SMBIOS table not found");
    return Status;
  }

  if (Type == MAX_UINT16) {
    OutEntryPoint (&Table);
  }

  BdtOutListBegin (L"Structures");
  S     = NULL;
  Shown = 0;
  for (Count = 0; Count < BDT_SMBIOS_MAX_STRUCTURES; Count++) {
    S = NextStructure (&Table, S);
    if (S == NULL) {
      break;
    }

    if ((Type == MAX_UINT16) || (S->Type == Type)) {
      DecodeStructure (&Table, S);
      Shown++;
    }
  }

  BdtOutListEnd ();
  if ((Type != MAX_UINT16) && (Shown == 0)) {
    BdtOutError (L"No SMBIOS structure of type %d", Type);
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
ValidateRow (
  IN     CONST CHAR16  *Check,
  IN     CONST CHAR16  *Result,
  IN     CONST CHAR16  *Detail,
  IN OUT UINTN         *Failures
  )
{
  BdtOutRowBegin ();
  BdtOutCell (L"%s", Check);
  BdtOutCell (L"%s", Result);
  BdtOutCell (L"%s", Detail);
  BdtOutRowEnd ();
  if (StrCmp (Result, L"FAIL") == 0) {
    (*Failures)++;
  }
}

EFI_STATUS
EFIAPI
BdtSmbiosValidate (
  VOID
  )
{
  STATIC CONST BDT_COLUMN  Columns[] = {
    { L"check",  L"Check",  26 },
    { L"result", L"Result", 8  },
    { L"detail", L"Detail", 0  },
  };
  STATIC CONST UINT8  Required[] = { 0, 1, 3, 4, 7, 9, 16, 17, 19, 32 };
  BDT_SMBIOS_TABLE    Table;
  SMBIOS_STRUCTURE    *S;
  SMBIOS_STRUCTURE    *Other;
  EFI_STATUS          Status;
  UINTN               Count;
  UINTN               Index;
  UINTN               Failures;
  UINTN               Duplicates;
  BOOLEAN             EndFound;
  UINT8               *Last;
  CHAR16              Detail[96];

  Status = GetTable (&Table);
  if (EFI_ERROR (Status)) {
    BdtOutError (L"SMBIOS table not found");
    return Status;
  }

  Failures = 0;
  BdtOutTableBegin (L"validation", Columns, ARRAY_SIZE (Columns));

  ValidateRow (
    L"Entry Point Checksum",
    (CalculateSum8 (Table.EntryPoint, Table.EntryPointLength) == 0) ? L"PASS" : L"FAIL",
    Table.Is3x ? L"_SM3_" : L"_SM_",
    &Failures
    );
  if (!Table.Is3x) {
    ValidateRow (
      L"Intermediate Checksum",
      (CalculateSum8 ((UINT8 *)Table.EntryPoint + 0x10, 0x0F) == 0) ? L"PASS" : L"FAIL",
      L"_DMI_",
      &Failures
      );
  }

  //
  // Walk the table and check structure / string-set integrity.
  //
  S        = NULL;
  Last     = Table.Start;
  EndFound = FALSE;
  for (Count = 0; Count < BDT_SMBIOS_MAX_STRUCTURES; Count++) {
    S = NextStructure (&Table, S);
    if (S == NULL) {
      break;
    }

    Last = (UINT8 *)S;
    if (S->Type == SMBIOS_TYPE_END) {
      EndFound = TRUE;
      Count++;
      break;
    }
  }

  UnicodeSPrint (Detail, sizeof (Detail), L"%d structures, last at offset 0x%x", Count, Last - Table.Start);
  ValidateRow (L"Structure Walk", EndFound ? L"PASS" : L"FAIL", Detail, &Failures);
  ValidateRow (L"End-of-Table (Type 127)", EndFound ? L"PASS" : L"FAIL", EndFound ? L"present" : L"missing or table corrupted", &Failures);

  if (!Table.Is3x) {
    UnicodeSPrint (Detail, sizeof (Detail), L"declared %d, found %d", Table.DeclaredCount, Count);
    ValidateRow (L"Structure Count", (Table.DeclaredCount == Count) ? L"PASS" : L"WARN", Detail, &Failures);
  }

  //
  // Duplicate handles.
  //
  Duplicates = 0;
  S          = NULL;
  for (Index = 0; Index < Count; Index++) {
    S = NextStructure (&Table, S);
    if (S == NULL) {
      break;
    }

    for (Other = NextStructure (&Table, S); Other != NULL; Other = NextStructure (&Table, Other)) {
      if (Other->Handle == S->Handle) {
        Duplicates++;
      }
    }
  }

  UnicodeSPrint (Detail, sizeof (Detail), L"%d duplicate(s)", Duplicates);
  ValidateRow (L"Unique Handles", (Duplicates == 0) ? L"PASS" : L"FAIL", Detail, &Failures);

  //
  // Structures required by the SMBIOS specification.
  //
  for (Index = 0; Index < ARRAY_SIZE (Required); Index++) {
    UnicodeSPrint (Detail, sizeof (Detail), L"Type %d %s", Required[Index], TypeName (Required[Index]));
    ValidateRow (L"Required Structure", (FindType (&Table, Required[Index]) != NULL) ? L"PASS" : L"WARN", Detail, &Failures);
  }

  BdtOutTableEnd ();
  BdtOutFieldNum (L"Failures", Failures);
  BdtOutField (L"Result", L"%s", (Failures == 0) ? L"PASS" : L"FAIL");
  return EFI_SUCCESS;
}
