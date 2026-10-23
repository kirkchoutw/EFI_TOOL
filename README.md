# EFI_TOOL
A collection of UEFI applications and utilities for firmware development, debugging, validation, and platform bring-up.

## BIOS EFI Debug Toolkit (BdtPkg)

`BDT.efi` is a read-only UEFI Shell application for inspecting a platform:
system summary, boot options, PCI/PCIe, memory map, UEFI variables,
handles/protocols/device paths, ACPI, SMBIOS, CPU (CPUID/MSR/MTRR/MCA),
firmware volumes, Secure Boot and TPM 2.0. Every command can print text,
CSV or JSON, and `collect` writes a complete debug package to disk.

### Build

Requires an edk2 checkout with BaseTools built (tested with GCC, X64 and IA32).

```sh
EDK2_DIR=~/edk2 ./build.sh X64 DEBUG      # or: ./build.sh IA32 RELEASE
```

or manually from the edk2 workspace:

```sh
export PACKAGES_PATH=$PWD:/path/to/EFI_TOOL
source edksetup.sh
build -p BdtPkg/BdtPkg.dsc -a X64 -t GCC -b DEBUG
```

Output: `$WORKSPACE/Build/BdtPkg/DEBUG_GCC/X64/Bdt.efi`

### Usage (UEFI Shell)

```
BDT.efi help [command]
BDT.efi version
BDT.efi system
BDT.efi boot order | option | dump <number> | timing | modules
BDT.efi pci list | tree | dump <BDF> [--ext] | cap <BDF> | bar <BDF> | link <BDF> | aer [BDF]
BDT.efi memory map | info | dump <address> <length> | search <pattern> [start] [length]
BDT.efi var list | get <name> [guid] | dump <name> [guid] | usage | save <dir> | diff <file1> <file2>
BDT.efi handle list | info <index> | protocol <guid> | tree
BDT.efi protocol list | find <guid>
BDT.efi devicepath list | decode <handle index>
BDT.efi acpi list | dump <signature> [instance] | find <string> | validate | save <dir>
BDT.efi smbios list | dump | type <type> | validate
BDT.efi cpu info | cpuid [leaf [subleaf]] | msr <address> | mtrr | mca
BDT.efi firmware fv | ffs [fv index] | find <guid>
BDT.efi security secureboot | pk | kek | db | dbx | dbt | dbr | tpm | pcr [index] | tcglog
BDT.efi io mmio read <address> <length> [width] | pci read <BDF> <offset> [width] | msr read <address>
BDT.efi collect [--quick | --full] [--output <directory>]
```

Global options: `--format text|csv|json` (`-f`), `--output <file>` (`-o`).
BDF is hex `[seg:]bus:dev.func`; GUIDs are accepted as text or known names
such as `EFI_PCI_IO_PROTOCOL` / `EFI_GLOBAL_VARIABLE`.

`collect` creates `BDT_YYYYMMDD_HHMMSS/` (in the current directory or under
`--output`) with `manifest.txt` and `system/ boot/ pci/ acpi/ uefi/ cpu/ security/`;
`--full` also saves raw ACPI tables and variable data.

### Layout

```
BdtPkg/
  BdtPkg.dec / BdtPkg.dsc
  Include/            Bdt.h, BdtCommon.h, BdtCommand.h, Library/*.h
  Application/Bdt/    BDT.efi (command parser, system, collect)
  Library/            BdtLib, BdtOutputLib, BdtFileLib, BdtPciLib, BdtMemoryLib,
                      BdtVariableLib, BdtHandleLib, BdtAcpiLib, BdtSmbiosLib,
                      BdtCpuLib, BdtSecurityLib, BdtBootLib, BdtFirmwareLib
```

### Notes

- Read only: no command writes hardware registers, MSRs or variables.
  `io ... write` is rejected.
- MSR reads install a temporary #GP handler, so an unsupported MSR returns
  an error instead of hanging (when the CPU arch protocol allows it).
- `boot timing` needs the platform to publish the ACPI FPDT
  (FirmwarePerformanceDxe + a PerformanceLib).
- Not implemented yet: `boot trace` (needs the planned BdtDxe driver),
  `acpi diff`, `firmware verify`, GPIO, debug log, BdtDxe/BdtPei.
