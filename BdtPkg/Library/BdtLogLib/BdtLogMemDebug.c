/** @file
  BdtLogLib - edk2 OvmfPkg MemDebugLog backend.

  OVMF built with DEBUG_TO_MEM=TRUE copies every DEBUG() message into a
  circular buffer whose address PEI passes to DXE in a GUID HOB. The
  layout below follows OvmfPkg/Include/Library/MemDebugLogLib.h.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include "BdtLogInternal.h"
#include <Library/SynchronizationLib.h>

#define MEM_DEBUG_LOG_MAGIC1     0x3167646d666d766fULL  // "ovmfmdg1"
#define MEM_DEBUG_LOG_MAGIC2     0x3267646d666d766fULL  // "ovmfmdg2"
#define MEM_DEBUG_LOG_MAX_SIZE   SIZE_16MB

#pragma pack(1)
typedef struct {
  UINT64             Magic1;
  UINT64             Magic2;
  UINT64             HeaderSize;
  UINT64             DebugLogSize;
  volatile UINT64    MemDebugLogLock;
  UINT64             DebugLogHeadOffset;
  UINT64             DebugLogTailOffset;
  UINT64             Truncated;
  CHAR8              FirmwareVersion[128];
} MEM_DEBUG_LOG_HDR;
#pragma pack()

//
// gMemDebugLogHobGuid (OvmfPkg.dec)
//
STATIC CONST EFI_GUID  mMemDebugLogHobGuid = {
  0x95305139, 0xb20f, 0x4723, { 0x84, 0x25, 0x62, 0x7c, 0x88, 0x8f, 0xf1, 0x21 }
};

STATIC
BOOLEAN
HeaderValid (
  IN CONST MEM_DEBUG_LOG_HDR  *Header
  )
{
  return (Header->Magic1 == MEM_DEBUG_LOG_MAGIC1) && (Header->Magic2 == MEM_DEBUG_LOG_MAGIC2) &&
         (Header->HeaderSize >= OFFSET_OF (MEM_DEBUG_LOG_HDR, FirmwareVersion)) &&
         (Header->HeaderSize <= SIZE_64KB) &&
         (Header->DebugLogSize > 0) && (Header->DebugLogSize <= MEM_DEBUG_LOG_MAX_SIZE) &&
         (Header->DebugLogHeadOffset < Header->DebugLogSize) &&
         (Header->DebugLogTailOffset < Header->DebugLogSize);
}

STATIC
EFI_STATUS
MemDebugLocate (
  OUT BDT_LOG_SOURCE  *Source
  )
{
  EFI_PHYSICAL_ADDRESS  *Address;
  UINTN                 DataSize;
  MEM_DEBUG_LOG_HDR     *Header;
  UINT64                Head;
  UINT64                Tail;

  Address = BdtLogFindGuidHob (&mMemDebugLogHobGuid, &DataSize);
  if ((Address == NULL) || (DataSize < sizeof (EFI_PHYSICAL_ADDRESS)) ||
      (*Address == 0) || (*Address > MAX_ADDRESS))
  {
    return EFI_NOT_FOUND;
  }

  Header = (MEM_DEBUG_LOG_HDR *)(UINTN)*Address;
  if (!HeaderValid (Header)) {
    return EFI_VOLUME_CORRUPTED;
  }

  Head             = Header->DebugLogHeadOffset;
  Tail             = Header->DebugLogTailOffset;
  Source->Address  = *Address;
  Source->Header   = Header;
  Source->Capacity = Header->DebugLogSize;
  Source->Used     = (Head <= Tail) ? (Tail - Head) : (Header->DebugLogSize - Head + Tail);
  Source->Lost     = (BOOLEAN)(Header->Truncated != 0);
  StrCpyS (Source->Found, ARRAY_SIZE (Source->Found), L"HOB gMemDebugLogHobGuid");
  StrCpyS (Source->Version, ARRAY_SIZE (Source->Version), L"1");
  return EFI_SUCCESS;
}

/**
  The writer keeps Tail as the next free byte. When the buffer has wrapped,
  Head is Tail + 1 and the text runs Head..end, then start..Tail.
**/
STATIC
EFI_STATUS
MemDebugRead (
  IN BDT_LOG_SOURCE  *Source,
  IN BDT_LOG_SINK    Sink,
  IN VOID            *Context
  )
{
  MEM_DEBUG_LOG_HDR  *Header;
  CONST CHAR8        *Start;
  CHAR8              *Copy;
  UINT64             Size;
  UINT64             Head;
  UINT64             Tail;
  UINTN              Length;
  BDT_LOG_ATTR       Attr;

  Header = Source->Header;
  if (!HeaderValid (Header)) {
    return EFI_VOLUME_CORRUPTED;
  }

  //
  // Snapshot first: firmware keeps writing while we read.
  //
  Size = Header->DebugLogSize;
  Head = Header->DebugLogHeadOffset;
  Tail = Header->DebugLogTailOffset;
  Copy = AllocatePool ((UINTN)Size);
  if (Copy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Start = (CONST CHAR8 *)Header + Header->HeaderSize;
  CopyMem (Copy, Start, (UINTN)Size);

  ZeroMem (&Attr, sizeof (Attr));
  if (Head <= Tail) {
    Sink (Context, Copy + Head, (UINTN)(Tail - Head), &Attr);
  } else {
    Length = (UINTN)(Size - Head);
    Sink (Context, Copy + Head, Length, &Attr);
    Sink (Context, Copy, (UINTN)Tail, &Attr);
  }

  FreePool (Copy);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
MemDebugClear (
  IN BDT_LOG_SOURCE  *Source
  )
{
  MEM_DEBUG_LOG_HDR  *Header;

  Header = Source->Header;
  if (!HeaderValid (Header)) {
    return EFI_VOLUME_CORRUPTED;
  }

  //
  // Take the writer's spin lock (a SynchronizationLib SPIN_LOCK, as in
  // MemDebugLogCommon.c) so an AP cannot write in between.
  //
  if (!AcquireSpinLockOrFail ((SPIN_LOCK *)&Header->MemDebugLogLock)) {
    return EFI_NOT_READY;
  }

  Header->DebugLogHeadOffset = 0;
  Header->DebugLogTailOffset = 0;
  Header->Truncated          = 0;
  ReleaseSpinLock ((SPIN_LOCK *)&Header->MemDebugLogLock);
  return EFI_SUCCESS;
}

CONST BDT_LOG_BACKEND  gBdtLogMemDebugLog = {
  L"MemDebugLog",
  MemDebugLocate,
  MemDebugRead,
  MemDebugClear,
  NULL
};
