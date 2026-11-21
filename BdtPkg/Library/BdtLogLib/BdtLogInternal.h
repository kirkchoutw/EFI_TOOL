/** @file
  BdtLogLib - backend interface.

  A backend locates its buffer, takes a snapshot of it and feeds the text
  to a sink in time order. Each chunk may carry a debug level, boot phase
  and time stamp; chunks are joined into lines by the core.

  To add a backend: implement BDT_LOG_BACKEND and list it in mBackends
  (BdtLogLib.c).

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_LOG_INTERNAL_H_
#define BDT_LOG_INTERNAL_H_

#include <BdtCommon.h>
#include <Library/BdtLogLib.h>

//
// Debug levels (MdePkg DebugLib.h values, used by AdvancedLogger entries)
//
#define BDT_LOG_LEVEL_ERROR    0x80000000
#define BDT_LOG_LEVEL_WARN     0x00000002
#define BDT_LOG_LEVEL_INFO     0x00000040
#define BDT_LOG_LEVEL_VERBOSE  0x00400000

typedef struct {
  CONST CHAR16            *Backend;
  EFI_PHYSICAL_ADDRESS    Address;        ///< header of the log buffer
  CHAR16                  Found[48];      ///< how it was found
  CHAR16                  Version[16];
  UINT64                  Capacity;       ///< bytes available for the log
  UINT64                  Used;           ///< bytes in use
  BOOLEAN                 Lost;           ///< older data was dropped
  UINT64                  LostBytes;      ///< known number of dropped bytes
  UINT64                  TimerFrequency; ///< ticks per second, 0 = none
  VOID                    *Header;
} BDT_LOG_SOURCE;

typedef struct {
  BOOLEAN    HasLevel;
  UINT32     Level;
  UINT16     Phase;
  UINT64     TimeStamp;
} BDT_LOG_ATTR;

typedef
VOID
(*BDT_LOG_SINK)(
  IN VOID                *Context,
  IN CONST CHAR8         *Text,
  IN UINTN               Length,
  IN CONST BDT_LOG_ATTR  *Attr
  );

typedef struct {
  CONST CHAR16    *Name;

  /**
    Find the buffer. EFI_NOT_FOUND when this firmware does not have it.
  **/
  EFI_STATUS (*Locate)(OUT BDT_LOG_SOURCE *Source);

  /**
    Snapshot the buffer and send its text to Sink, oldest first.
  **/
  EFI_STATUS (*Read)(IN BDT_LOG_SOURCE *Source, IN BDT_LOG_SINK Sink, IN VOID *Context);

  /**
    Empty the buffer.
  **/
  EFI_STATUS (*Clear)(IN BDT_LOG_SOURCE *Source);

  /**
    Name of a boot phase number, or NULL.
  **/
  CONST CHAR16 *(*PhaseName)(IN UINT16 Phase);
} BDT_LOG_BACKEND;

extern CONST BDT_LOG_BACKEND  gBdtLogMemDebugLog;
extern CONST BDT_LOG_BACKEND  gBdtLogAdvancedLogger;

/**
  Data of the first GUID HOB with Guid, or NULL. DataSize is optional.
**/
VOID *
BdtLogFindGuidHob (
  IN  CONST EFI_GUID  *Guid,
  OUT UINTN           *DataSize OPTIONAL
  );

#endif
