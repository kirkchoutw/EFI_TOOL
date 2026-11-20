/** @file
  BdtLib - SHA-256 (FIPS 180-4), small and dependency free.

  Copyright (c) 2026, Kirk Chou. All rights reserved.<BR>
  SPDX-License-Identifier: MIT
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BdtLib.h>

STATIC CONST UINT32  mK[64] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n)  (((x) >> (n)) | ((x) << (32 - (n))))

STATIC
VOID
Sha256Block (
  IN OUT BDT_SHA256_CONTEXT  *Context,
  IN     CONST UINT8         *Block
  )
{
  UINT32  W[64];
  UINT32  A, B, C, D, E, F, G, H;
  UINT32  T1, T2;
  UINTN   Index;

  for (Index = 0; Index < 16; Index++) {
    W[Index] = ((UINT32)Block[Index * 4] << 24) | ((UINT32)Block[Index * 4 + 1] << 16) |
               ((UINT32)Block[Index * 4 + 2] << 8) | (UINT32)Block[Index * 4 + 3];
  }

  for (Index = 16; Index < 64; Index++) {
    W[Index] = (ROTR (W[Index - 2], 17) ^ ROTR (W[Index - 2], 19) ^ (W[Index - 2] >> 10)) + W[Index - 7] +
               (ROTR (W[Index - 15], 7) ^ ROTR (W[Index - 15], 18) ^ (W[Index - 15] >> 3)) + W[Index - 16];
  }

  A = Context->State[0];
  B = Context->State[1];
  C = Context->State[2];
  D = Context->State[3];
  E = Context->State[4];
  F = Context->State[5];
  G = Context->State[6];
  H = Context->State[7];
  for (Index = 0; Index < 64; Index++) {
    T1 = H + (ROTR (E, 6) ^ ROTR (E, 11) ^ ROTR (E, 25)) + ((E & F) ^ (~E & G)) + mK[Index] + W[Index];
    T2 = (ROTR (A, 2) ^ ROTR (A, 13) ^ ROTR (A, 22)) + ((A & B) ^ (A & C) ^ (B & C));
    H  = G;
    G  = F;
    F  = E;
    E  = D + T1;
    D  = C;
    C  = B;
    B  = A;
    A  = T1 + T2;
  }

  Context->State[0] += A;
  Context->State[1] += B;
  Context->State[2] += C;
  Context->State[3] += D;
  Context->State[4] += E;
  Context->State[5] += F;
  Context->State[6] += G;
  Context->State[7] += H;
}

VOID
EFIAPI
BdtSha256Init (
  OUT BDT_SHA256_CONTEXT  *Context
  )
{
  ZeroMem (Context, sizeof (*Context));
  Context->State[0] = 0x6a09e667;
  Context->State[1] = 0xbb67ae85;
  Context->State[2] = 0x3c6ef372;
  Context->State[3] = 0xa54ff53a;
  Context->State[4] = 0x510e527f;
  Context->State[5] = 0x9b05688c;
  Context->State[6] = 0x1f83d9ab;
  Context->State[7] = 0x5be0cd19;
}

VOID
EFIAPI
BdtSha256Update (
  IN OUT BDT_SHA256_CONTEXT  *Context,
  IN     CONST VOID          *Data,
  IN     UINTN               Size
  )
{
  CONST UINT8  *Bytes;
  UINTN        Take;

  Bytes            = (CONST UINT8 *)Data;
  Context->Length += Size;
  while (Size > 0) {
    if ((Context->Used == 0) && (Size >= 64)) {
      Sha256Block (Context, Bytes);
      Bytes += 64;
      Size  -= 64;
      continue;
    }

    Take = MIN (Size, 64 - Context->Used);
    CopyMem (&Context->Buffer[Context->Used], Bytes, Take);
    Context->Used += Take;
    Bytes         += Take;
    Size          -= Take;
    if (Context->Used == 64) {
      Sha256Block (Context, Context->Buffer);
      Context->Used = 0;
    }
  }
}

VOID
EFIAPI
BdtSha256Final (
  IN OUT BDT_SHA256_CONTEXT  *Context,
  OUT    UINT8               Digest[BDT_SHA256_SIZE]
  )
{
  UINT64  Bits;
  UINTN   Index;

  Bits                             = Context->Length * 8;
  Context->Buffer[Context->Used++] = 0x80;
  if (Context->Used > 56) {
    ZeroMem (&Context->Buffer[Context->Used], 64 - Context->Used);
    Sha256Block (Context, Context->Buffer);
    Context->Used = 0;
  }

  ZeroMem (&Context->Buffer[Context->Used], 56 - Context->Used);
  for (Index = 0; Index < 8; Index++) {
    Context->Buffer[63 - Index] = (UINT8)RShiftU64 (Bits, Index * 8);
  }

  Sha256Block (Context, Context->Buffer);
  for (Index = 0; Index < 8; Index++) {
    Digest[Index * 4]     = (UINT8)(Context->State[Index] >> 24);
    Digest[Index * 4 + 1] = (UINT8)(Context->State[Index] >> 16);
    Digest[Index * 4 + 2] = (UINT8)(Context->State[Index] >> 8);
    Digest[Index * 4 + 3] = (UINT8)Context->State[Index];
  }
}

VOID
EFIAPI
BdtSha256 (
  IN  CONST VOID  *Data,
  IN  UINTN       Size,
  OUT UINT8       Digest[BDT_SHA256_SIZE]
  )
{
  BDT_SHA256_CONTEXT  Context;

  BdtSha256Init (&Context);
  BdtSha256Update (&Context, Data, Size);
  BdtSha256Final (&Context, Digest);
}
