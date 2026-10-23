/** @file
  BIOS EFI Debug Toolkit (BDT) - command framework definitions.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#ifndef BDT_COMMAND_H_
#define BDT_COMMAND_H_

#include <BdtCommon.h>

///
/// Global options parsed by the command framework before dispatch.
///
typedef struct {
  BDT_OUTPUT_FORMAT    Format;
  CONST CHAR16         *OutputPath;   ///< --output value, NULL when not given.
} BDT_CONTEXT;

/**
  Handler of one top level command.

  @param[in] Context  Global options.
  @param[in] Argc     Number of arguments after the command name.
  @param[in] Argv     Arguments after the command name (Argv[0] is the sub command).

  @retval EFI_SUCCESS            Command completed.
  @retval EFI_INVALID_PARAMETER  Bad usage. The framework prints the usage text.
  @retval Others                 Command failed.
**/
typedef
EFI_STATUS
(*BDT_COMMAND_HANDLER)(
  IN BDT_CONTEXT  *Context,
  IN UINTN        Argc,
  IN CHAR16       **Argv
  );

typedef struct {
  CONST CHAR16           *Name;
  CONST CHAR16           *Summary;
  CONST CHAR16           *Usage;
  BDT_COMMAND_HANDLER    Handler;
} BDT_COMMAND;

#endif
