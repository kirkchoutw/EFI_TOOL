## @file
#  BIOS EFI Debug Toolkit (BDT) platform description.
#
#  Build (from an edk2 workspace):
#    export PACKAGES_PATH=$WORKSPACE:<path containing BdtPkg>
#    build -p BdtPkg/BdtPkg.dsc -a X64 -t GCC -b DEBUG
#
#  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
#  SPDX-License-Identifier: MIT
##

[Defines]
  PLATFORM_NAME                  = BdtPkg
  PLATFORM_GUID                  = 437B377D-668B-42D0-B028-A7DC6B737961
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/BdtPkg
  SUPPORTED_ARCHITECTURES        = IA32|X64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

!include MdePkg/MdeLibs.dsc.inc

[LibraryClasses]
  #
  # Entry point
  #
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  ShellCEntryLib|ShellPkg/Library/UefiShellCEntryLib/UefiShellCEntryLib.inf

  #
  # MdePkg / MdeModulePkg / ShellPkg
  #
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLibOptionalDevicePathProtocol.inf
  FileHandleLib|MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
  SortLib|MdeModulePkg/Library/UefiSortLib/UefiSortLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf

  #
  # BdtPkg
  #
  BdtLib|BdtPkg/Library/BdtLib/BdtLib.inf
  BdtOutputLib|BdtPkg/Library/BdtOutputLib/BdtOutputLib.inf
  BdtFileLib|BdtPkg/Library/BdtFileLib/BdtFileLib.inf
  BdtPciLib|BdtPkg/Library/BdtPciLib/BdtPciLib.inf
  BdtMemoryLib|BdtPkg/Library/BdtMemoryLib/BdtMemoryLib.inf
  BdtVariableLib|BdtPkg/Library/BdtVariableLib/BdtVariableLib.inf
  BdtHandleLib|BdtPkg/Library/BdtHandleLib/BdtHandleLib.inf
  BdtAcpiLib|BdtPkg/Library/BdtAcpiLib/BdtAcpiLib.inf
  BdtSmbiosLib|BdtPkg/Library/BdtSmbiosLib/BdtSmbiosLib.inf
  BdtCpuLib|BdtPkg/Library/BdtCpuLib/BdtCpuLib.inf
  BdtSecurityLib|BdtPkg/Library/BdtSecurityLib/BdtSecurityLib.inf
  BdtBootLib|BdtPkg/Library/BdtBootLib/BdtBootLib.inf
  BdtFirmwareLib|BdtPkg/Library/BdtFirmwareLib/BdtFirmwareLib.inf

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdUefiLibMaxPrintBufferSize|16000

[Components]
  BdtPkg/Library/BdtLib/BdtLib.inf
  BdtPkg/Library/BdtOutputLib/BdtOutputLib.inf
  BdtPkg/Library/BdtFileLib/BdtFileLib.inf
  BdtPkg/Library/BdtPciLib/BdtPciLib.inf
  BdtPkg/Library/BdtMemoryLib/BdtMemoryLib.inf
  BdtPkg/Library/BdtVariableLib/BdtVariableLib.inf
  BdtPkg/Library/BdtHandleLib/BdtHandleLib.inf
  BdtPkg/Library/BdtAcpiLib/BdtAcpiLib.inf
  BdtPkg/Library/BdtSmbiosLib/BdtSmbiosLib.inf
  BdtPkg/Library/BdtCpuLib/BdtCpuLib.inf
  BdtPkg/Library/BdtSecurityLib/BdtSecurityLib.inf
  BdtPkg/Library/BdtBootLib/BdtBootLib.inf
  BdtPkg/Library/BdtFirmwareLib/BdtFirmwareLib.inf

  BdtPkg/Application/Bdt/Bdt.inf
