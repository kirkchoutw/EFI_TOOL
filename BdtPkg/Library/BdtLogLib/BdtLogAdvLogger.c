/** @file
  BdtLogLib - Project Mu AdvancedLogger backend.

  Layouts follow mu_plus AdvLoggerPkg/Include/AdvancedLoggerInternal.h,
  Protocol/AdvancedLogger.h and AdvancedLoggerInternalProtocol.h.

  The logger info block is found, in order, through:
    1. the Advanced Logger protocol (its container holds the pointer),
    2. the "AdvLoggerLocator" variable,
    3. the Advanced Logger HOB (ADVANCED_LOGGER_PTR).

  The log is a linear array of 8 byte aligned message entries ('ALMS' v1
  or 'ALM2' v2) from LogBufferOffset to LogCurrentOffset. When it is full,
  new messages are dropped and counted in DiscardedSize.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include "BdtLogInternal.h"

#define ADVANCED_LOGGER_SIGNATURE           SIGNATURE_32 ('A','L','O','G')
#define ADVANCED_LOGGER_PROTOCOL_SIGNATURE  SIGNATURE_32 ('L','O','G','P')
#define ADVANCED_LOGGER_PTR_SIGNATURE       SIGNATURE_64 ('A','l','o','g','_','P','t','r')
#define MESSAGE_ENTRY_SIGNATURE             SIGNATURE_32 ('A','L','M','S')
#define MESSAGE_ENTRY_SIGNATURE_V2          SIGNATURE_32 ('A','L','M','2')
#define ADV_LOG_MAX_SIZE                    SIZE_64MB

#pragma pack(1)

//
// Current layout (offsets relative to the info block).
//
typedef struct {
  UINT32                  Signature;
  UINT16                  Version;
  UINT16                  Reserved[3];
  UINT32                  LogBufferOffset;
  UINT32                  Reserved4;
  UINT32                  LogCurrentOffset;
  UINT32                  DiscardedSize;
  UINT32                  LogBufferSize;
  BOOLEAN                 InPermanentRAM;
  BOOLEAN                 AtRuntime;
  BOOLEAN                 GoneVirtual;
  BOOLEAN                 HdwPortInitialized;
  BOOLEAN                 HdwPortDisabled;
  BOOLEAN                 Reserved2[3];
  UINT64                  TimerFrequency;
  UINT64                  TicksAtTime;
  EFI_TIME                Time;
  UINT32                  HwPrintLevel;
  UINT32                  Reserved3;
  EFI_PHYSICAL_ADDRESS    Deprecated;
} ADV_LOGGER_INFO;

//
// Older layout with absolute addresses (best effort).
//
typedef struct {
  UINT32                  Signature;
  UINT16                  Version;
  UINT16                  Reserved[3];
  EFI_PHYSICAL_ADDRESS    LogBuffer;
  EFI_PHYSICAL_ADDRESS    LogCurrent;
  UINT32                  DiscardedSize;
  UINT32                  LogBufferSize;
} ADV_LOGGER_INFO_LEGACY;

typedef struct {
  UINT32    Signature;
  UINT32    DebugLevel;
  UINT64    TimeStamp;
  UINT16    MessageLen;
} ADV_LOGGER_MESSAGE_V1;

typedef struct {
  UINT32    Signature;
  UINT8     MajorVersion;
  UINT8     MinorVersion;
  UINT32    DebugLevel;
  UINT64    TimeStamp;
  UINT16    Phase;
  UINT16    MessageLen;
  UINT16    MessageOffset;
} ADV_LOGGER_MESSAGE_V2;

typedef struct {
  EFI_PHYSICAL_ADDRESS    LogBuffer;
  UINT64                  Signature;
} ADV_LOGGER_PTR;

#pragma pack()

//
// Not packed: the DXE library embeds the protocol in this container.
//
typedef struct {
  UINT32    Signature;
  UINT32    Version;
  VOID      *AdvancedLoggerWriteProtocol;
} ADV_LOGGER_PROTOCOL;

typedef struct {
  ADV_LOGGER_PROTOCOL    AdvLoggerProtocol;
  ADV_LOGGER_INFO        *LoggerInfo;
} ADV_LOGGER_PROTOCOL_CONTAINER;

STATIC CONST EFI_GUID  mAdvancedLoggerProtocolGuid = {
  0x434f695c, 0xef26, 0x4a12, { 0x9e, 0xba, 0xdd, 0xef, 0x00, 0x97, 0x49, 0x7c }
};

STATIC CONST EFI_GUID  mAdvancedLoggerHobGuid = {
  0x4d60cfb5, 0xf481, 0x4a98, { 0x9c, 0x81, 0xbf, 0xf8, 0x64, 0x60, 0xc4, 0x3e }
};

STATIC CONST CHAR16  *mPhaseNames[] = {
  L"-", L"SEC", L"PEI", L"PEI64", L"DXE", L"RUNTIME", L"MM_CORE", L"MM", L"SMM_CORE", L"SMM", L"TFA"
};

/**
  Decoded view of either layout.
**/
typedef struct {
  UINT8     *Base;      ///< info block
  BOOLEAN   Legacy;
  UINT64    Start;      ///< offset of the first entry
  UINT64    Current;    ///< offset of the next free byte
  UINT64    Limit;      ///< offset of the end of the buffer
  UINT32    Discarded;
} ADV_VIEW;

STATIC
BOOLEAN
GetView (
  IN  EFI_PHYSICAL_ADDRESS  Address,
  OUT ADV_VIEW              *View
  )
{
  ADV_LOGGER_INFO         *Info;
  ADV_LOGGER_INFO_LEGACY  *Legacy;

  if ((Address == 0) || (Address > MAX_ADDRESS) || ((Address & 7) != 0)) {
    return FALSE;
  }

  Info = (ADV_LOGGER_INFO *)(UINTN)Address;
  if (Info->Signature != ADVANCED_LOGGER_SIGNATURE) {
    return FALSE;
  }

  ZeroMem (View, sizeof (*View));
  View->Base = (UINT8 *)Info;

  //
  // In the current layout the 8 bytes at offset 12 are a small offset and a
  // zero; in the old one they are an address at or above the info block.
  //
  Legacy = (ADV_LOGGER_INFO_LEGACY *)Info;
  if ((Info->Version < 6) && (Legacy->LogBuffer >= Address)) {
    View->Legacy    = TRUE;
    View->Start     = Legacy->LogBuffer - Address;
    View->Current   = Legacy->LogCurrent - Address;
    View->Limit     = View->Start + Legacy->LogBufferSize;
    View->Discarded = Legacy->DiscardedSize;
  } else {
    View->Start     = Info->LogBufferOffset;
    View->Current   = Info->LogCurrentOffset;
    View->Limit     = (UINT64)Info->LogBufferOffset + Info->LogBufferSize;
    View->Discarded = Info->DiscardedSize;
  }

  if ((View->Start < sizeof (ADV_LOGGER_INFO_LEGACY)) || (View->Limit > ADV_LOG_MAX_SIZE) ||
      (View->Start > View->Limit))
  {
    return FALSE;
  }

  //
  // A full log can point past the end; clip it.
  //
  if (View->Current < View->Start) {
    View->Current = View->Start;
  }

  if (View->Current > View->Limit) {
    View->Current = View->Limit;
  }

  return TRUE;
}

STATIC
EFI_STATUS
AdvLocate (
  OUT BDT_LOG_SOURCE  *Source
  )
{
  EFI_STATUS                     Status;
  ADV_LOGGER_PROTOCOL            *Protocol;
  ADV_LOGGER_PTR                 *Ptr;
  EFI_PHYSICAL_ADDRESS           Address;
  UINTN                          Size;
  UINT64                         Value;
  ADV_VIEW                       View;
  ADV_LOGGER_INFO                *Info;

  Address = 0;
  Status  = gBS->LocateProtocol ((EFI_GUID *)&mAdvancedLoggerProtocolGuid, NULL, (VOID **)&Protocol);
  if (!EFI_ERROR (Status) && (Protocol != NULL) && (Protocol->Signature == ADVANCED_LOGGER_PROTOCOL_SIGNATURE)) {
    Address = (EFI_PHYSICAL_ADDRESS)(UINTN)((ADV_LOGGER_PROTOCOL_CONTAINER *)Protocol)->LoggerInfo;
    if (GetView (Address, &View)) {
      StrCpyS (Source->Found, ARRAY_SIZE (Source->Found), L"Advanced Logger protocol");
    } else {
      Address = 0;
    }
  }

  if (Address == 0) {
    Value = 0;
    Size  = sizeof (Value);
    if (!EFI_ERROR (gRT->GetVariable (L"AdvLoggerLocator", (EFI_GUID *)&mAdvancedLoggerHobGuid, NULL, &Size, &Value)) &&
        ((Size == sizeof (UINT32)) || (Size == sizeof (UINT64))) && GetView (Value, &View))
    {
      Address = Value;
      StrCpyS (Source->Found, ARRAY_SIZE (Source->Found), L"AdvLoggerLocator variable");
    }
  }

  if (Address == 0) {
    Ptr = BdtLogFindGuidHob (&mAdvancedLoggerHobGuid, &Size);
    if ((Ptr != NULL) && (Size >= sizeof (EFI_PHYSICAL_ADDRESS)) &&
        ((Size < sizeof (ADV_LOGGER_PTR)) || (Ptr->Signature == ADVANCED_LOGGER_PTR_SIGNATURE)) &&
        GetView (Ptr->LogBuffer, &View))
    {
      Address = Ptr->LogBuffer;
      StrCpyS (Source->Found, ARRAY_SIZE (Source->Found), L"HOB gAdvancedLoggerHobGuid");
    }
  }

  if (Address == 0) {
    return EFI_NOT_FOUND;
  }

  Info                   = (ADV_LOGGER_INFO *)(UINTN)Address;
  Source->Address        = Address;
  Source->Header         = Info;
  Source->Capacity       = View.Limit - View.Start;
  Source->Used           = View.Current - View.Start;
  Source->LostBytes      = View.Discarded;
  Source->Lost           = (BOOLEAN)(View.Discarded != 0);
  Source->TimerFrequency = View.Legacy ? 0 : Info->TimerFrequency;
  UnicodeSPrint (Source->Version, sizeof (Source->Version), L"%d%s", (UINTN)Info->Version, View.Legacy ? L" (legacy)" : L"");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
AdvRead (
  IN BDT_LOG_SOURCE  *Source,
  IN BDT_LOG_SINK    Sink,
  IN VOID            *Context
  )
{
  ADV_VIEW               View;
  UINT8                  *Copy;
  UINTN                  Size;
  UINTN                  Pos;
  UINTN                  Entry;
  UINT32                 Signature;
  ADV_LOGGER_MESSAGE_V1  *V1;
  ADV_LOGGER_MESSAGE_V2  *V2;
  BDT_LOG_ATTR           Attr;

  if (!GetView (Source->Address, &View)) {
    return EFI_VOLUME_CORRUPTED;
  }

  //
  // Snapshot the used part.
  //
  Size = (UINTN)(View.Current - View.Start);
  Copy = AllocatePool (Size + 1);
  if (Copy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (Copy, View.Base + View.Start, Size);

  Pos = 0;
  while (Pos + sizeof (UINT32) <= Size) {
    Signature = ReadUnaligned32 ((UINT32 *)(Copy + Pos));
    ZeroMem (&Attr, sizeof (Attr));
    Attr.HasLevel = TRUE;
    if ((Signature == MESSAGE_ENTRY_SIGNATURE_V2) && (Pos + sizeof (ADV_LOGGER_MESSAGE_V2) <= Size)) {
      V2 = (ADV_LOGGER_MESSAGE_V2 *)(Copy + Pos);
      if ((V2->MessageOffset < sizeof (ADV_LOGGER_MESSAGE_V2)) || (Pos + V2->MessageOffset + V2->MessageLen > Size)) {
        break;
      }

      Attr.Level     = V2->DebugLevel;
      Attr.Phase     = V2->Phase;
      Attr.TimeStamp = V2->TimeStamp;
      Sink (Context, (CHAR8 *)V2 + V2->MessageOffset, V2->MessageLen, &Attr);
      Entry = ALIGN_VALUE ((UINTN)V2->MessageOffset + V2->MessageLen, 8);
    } else if ((Signature == MESSAGE_ENTRY_SIGNATURE) && (Pos + sizeof (ADV_LOGGER_MESSAGE_V1) <= Size)) {
      V1 = (ADV_LOGGER_MESSAGE_V1 *)(Copy + Pos);
      if (Pos + sizeof (ADV_LOGGER_MESSAGE_V1) + V1->MessageLen > Size) {
        break;
      }

      Attr.Level     = V1->DebugLevel;
      Attr.TimeStamp = V1->TimeStamp;
      Sink (Context, (CHAR8 *)(V1 + 1), V1->MessageLen, &Attr);
      Entry = ALIGN_VALUE (sizeof (ADV_LOGGER_MESSAGE_V1) + V1->MessageLen, 8);
    } else {
      //
      // Not an entry: the rest cannot be trusted.
      //
      break;
    }

    Pos += Entry;
  }

  FreePool (Copy);
  return (Pos + sizeof (UINT32) <= Size) ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
}

STATIC
EFI_STATUS
AdvClear (
  IN BDT_LOG_SOURCE  *Source
  )
{
  ADV_VIEW                View;
  ADV_LOGGER_INFO         *Info;
  ADV_LOGGER_INFO_LEGACY  *Legacy;

  if (!GetView (Source->Address, &View)) {
    return EFI_VOLUME_CORRUPTED;
  }

  if (View.Legacy) {
    Legacy                = (ADV_LOGGER_INFO_LEGACY *)View.Base;
    Legacy->LogCurrent    = Legacy->LogBuffer;
    Legacy->DiscardedSize = 0;
  } else {
    Info                   = (ADV_LOGGER_INFO *)View.Base;
    Info->LogCurrentOffset = Info->LogBufferOffset;
    Info->DiscardedSize    = 0;
  }

  return EFI_SUCCESS;
}

STATIC
CONST CHAR16 *
AdvPhaseName (
  IN UINT16  Phase
  )
{
  return (Phase < ARRAY_SIZE (mPhaseNames)) ? mPhaseNames[Phase] : NULL;
}

CONST BDT_LOG_BACKEND  gBdtLogAdvancedLogger = {
  L"AdvancedLogger",
  AdvLocate,
  AdvRead,
  AdvClear,
  AdvPhaseName
};
