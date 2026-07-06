/*
 * MC9S08GT (U2) — CRC-16 update, Ghidra disassembly (raw).
 *
 * FUN_e0cd folds one BYTE into the running CRC by processing its two nibbles
 * through FUN_e08e. FUN_e08e is a nibble-indexed table update using two 16-entry
 * tables at flash 0x8b14 (hi) and 0x8b24 (lo). Combined with the 0xFFFF seed
 * (FUN_e088) and MSB-first byte order, this reproduces CRC-16/CCITT-FALSE
 * (poly 0x1021, init 0xFFFF, no reflect, xorout 0) — confirmed on-air by
 * tools/rtl433/probe_capture.py and by the rtl_433 decoder's crc16() gate.
 *
 * This is why the rtl_433 decoder calls the field "mic" (Message Integrity
 * Check) — it is the frame's CRC.
 */

/* ---- FUN_e0cd : update CRC with one byte (high nibble then low nibble) ----- */
/* aka crc16_update_byte. */
e0cd  PSHA
e0ce  AIS #-0x2
e0d0  STHX 0x1,SP
e0d3  NSA            ; swap nibbles
e0d4  AND #0xf
e0d6  BSR 0xe08e     ; fold high nibble
e0d8  LDHX 0x1,SP
e0db  LDA 0x3,SP
e0de  AND #0xf
e0e0  BSR 0xe08e     ; fold low nibble
e0e2  AIS #0x3
e0e4  RTS

/* ---- FUN_e08e : fold one nibble via the 256->16 CRC tables ----------------- */
/* aka crc16_fold_nibble. Called twice by crc16_update_byte, once per nibble. */
e08e  PSHX
e08f  PSHH
e090  PSHA
e091  LDA ,X
e092  PSHA
e093  NSA
e094  AND #0xf
e096  EOR 0x2,SP
e099  PSHA
e09a  LDA 0x2,SP
e09d  NSA
e09e  AND #0xf0
e0a0  STA 0x2,SP
e0a3  LDA 0x1,X
e0a5  NSA
e0a6  AND #0xf
e0a8  ORA 0x2,SP
e0ab  STA ,X
e0ac  LDA 0x1,X
e0ae  NSA
e0af  AND #0xf0
e0b1  STA 0x1,X
e0b3  LDA ,X
e0b4  TSX
e0b5  LDX ,X
e0b6  CLRH
e0b7  EOR 0x8b14,X   ; CRC table (hi byte)
e0ba  LDHX 0x4,SP
e0bd  STA ,X
e0be  LDA 0x1,X
e0c0  PULX
e0c1  CLRH
e0c2  EOR 0x8b24,X   ; CRC table (lo byte)
e0c5  LDHX 0x3,SP
e0c8  STA 0x1,X
e0ca  AIS #0x4
e0cc  RTS
