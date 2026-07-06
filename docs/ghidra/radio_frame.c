/*
 * MC9S08GT (U2) — radio TX path, Ghidra decompile (raw, lightly annotated).
 *
 * Source: dumps/u2-mc9s08gt-flash.bin. Addresses are RAM/flash addresses shown
 * by Ghidra (file_offset + 0x107f). These are the functions that BUILD the
 * 32-byte nRF905 payload and its CRC. The nRF9E5 (U1) 8051 is only a relay —
 * see docs/nrf9e5-firmware.md — so all payload semantics live here.
 *
 * Call chain for one radio message:
 *   FUN_db22 (queue_radio_message)      -> snapshots the correlation words +
 *                                           direction, then
 *   FUN_ced9 (assemble_and_send_frame)  -> assembles the frame, then
 *   FUN_ce01 (serialize_frame_and_crc)  -> serialises bytes and runs the CRC, then
 *   FUN_e0cd/e08e (crc16_update_byte/crc16_fold_nibble) -> CRC-16/CCITT-FALSE
 *                                           nibble update (tables 0x8b14 / 0x8b24).
 *
 * The names in parens are this file's own shorthand (not Ghidra's) - used in
 * the comments below and cross-referenced from other docs. The real,
 * addressable identifier is always the FUN_xxxx form.
 *
 * See docs/rtl433-decoder.md for the resulting on-air byte map.
 */

/* ---- FUN_db22 @ db22 : queue one radio message of a given "type" ---------- */
/* aka queue_radio_message(type). Snapshots the four poll/ack correlation      */
/* words (013e,0140,0109,010b) and the direction/marker bytes, stores the      */
/* message TYPE in DAT_06be/06bf, then calls the assembler FUN_ced9. local_6 = */
/* 0x40 is the direction marker (poll).                                        */
void FUN_db22(undefined1 param_1)   /* param_1 = message type (0x20..0x26) */
{
  char cVar1;
  undefined1 in_X;
  undefined2 local_f;
  undefined2 local_d;
  undefined2 local_b;
  undefined2 local_9;
  undefined1 uStack_7;
  undefined1 local_6;
  undefined1 local_4;
  undefined1 *local_2;

  if (DAT_06e9 != '\0') {          /* radio enabled */
    local_d = _DAT_0140;           /* correlation word 2 -> payload[5:7] */
    local_f = _DAT_013e;           /* correlation word 1 -> payload[4]   */
    local_9 = _DAT_010b;           /* correlation word 4 -> payload[8:10]*/
    local_b = _DAT_0109;           /* correlation word 3 -> payload[7]   */
    uStack_7 = 0;
    local_6 = 0x40;                /* direction marker: 0x40 = poll */
    local_4 = 2;
    local_2 = &DAT_06be;
    DAT_06be = in_X;               /* type high byte */
    DAT_06bf = param_1;            /* type          */
    FUN_ced9(&local_f);
    FUN_8fc4(0,0,0,0,0x1e,8);
    while( true ) {
      cVar1 = FUN_9120();
      if (cVar1 != '\0') break;
      SRS = 0;
    }
  }
  return;
}

/* ---- FUN_ced9 @ ced9 : assemble the frame into the SPI-out buffer ---------- */
/* aka assemble_and_send_frame. Copies fields with FUN_9fc9 (aka memcpy_bytes), */
/* sets the CRC anchor (066b=0x66d), calls FUN_ce01 to serialise+CRC, FUN_8eb2  */
/* (aka sci_kick_transmit) kicks the SCI/SPI transmitter. When the direction    */
/* byte at +9 == '@' (0x40, a poll) it also arms the ack-wait timer.            */
void FUN_ced9(undefined2 param_1)
{
  undefined1 uVar1, uVar2, in_stack_00000000, uStack_1;

  uVar2 = (undefined1)((ushort)param_1 >> 8);
  _uStack_1 = CONCAT11(uVar2,in_stack_00000000);
  while (DAT_064d != '\0') { uVar1 = FUN_d02f(); SRS = uVar1; }
  FUN_9fc9(uVar2,in_stack_00000000,6,0x5e,_uStack_1);
  FUN_9fc9((char)((ushort)*(undefined2 *)(_uStack_1 + 0xd) >> 8),
           (char)*(undefined2 *)(_uStack_1 + 0xd),6,0x6d);
  _DAT_066b = 0x66d;
  FUN_ce01();
  FUN_8eb2();
  if (*(char *)(_uStack_1 + 9) == '@') {   /* 0x40 poll -> expect a response */
    DAT_064e = 0;
    FUN_8fc4(1,&UNK_cf94,0,3,0xe8,9);
    DAT_068b = 1;
    DAT_064d = '\x01';
  }
  return;
}

/* ---- FUN_ce01 @ ce01 : serialise payload bytes + running CRC --------------- */
/* aka serialize_frame_and_crc. uStack_8/uStack_7 = the CRC accumulator,        */
/* seeded to 0xFFFF by FUN_e088 (aka crc16_seed).                                */
/* cStack_3 = payload[0] = record[8] + record[0xb] + 12.                         */
/* bStack_2 = payload[3] = ((record[8]&3)<<4) | record[9] | 0x0f  (record[9] is  */
/*            the direction: 0x40 poll / 0x80 response).                         */
/* FUN_8ec7 = push one byte to the SCI/SPI FIFO; FUN_cec7/ceb8/cec1 = push byte  */
/* AND fold it into the CRC. The two trailing FUN_8ec7(uStack_8/uStack_7) append */
/* the 16-bit CRC, MSB first.                                                    */
void FUN_ce01(undefined2 param_1)
{
  undefined1 in_stack_00000000;
  undefined1 local_b [3];
  undefined1 uStack_8;   /* CRC hi */
  undefined1 uStack_7;   /* CRC lo */
  byte local_6;
  undefined1 uStack_5, uStack_4;
  char cStack_3;         /* payload[0] */
  byte bStack_2;         /* payload[3] */
  undefined1 uStack_1;

  _uStack_1 = CONCAT11((char)((ushort)param_1 >> 8),in_stack_00000000);
  uStack_4 = 0;
  uStack_5 = 0;
  FUN_e088(&uStack_8);                 /* CRC = 0xFFFF */
  cStack_3 = *(char *)(_uStack_1 + 8) + *(char *)(_uStack_1 + 0xb) + '\f';
  FUN_8ec7();                          /* payload[1] = 0xCC (const, from caller) */
  FUN_8ec7();                          /* payload[2] = 0x6E (const)              */
  FUN_cec7(&uStack_8);                 /* payload[0] = cStack_3, +CRC            */
  bStack_2 = (*(byte *)(_uStack_1 + 8) & 3) << 4 | *(byte *)(_uStack_1 + 9) | 0xf;
  FUN_cec7(&uStack_8);                 /* payload[3] = bStack_2, +CRC            */
  FUN_ceb8(local_b); FUN_cec1();       /* payload[4]  = word_013e lo            */
  FUN_ceb8(local_b); FUN_cec1();       /* payload[5]                            */
  FUN_ceb8(local_b); FUN_cec1();       /* payload[6]  = word_0140              */
  FUN_ceb8(local_b); FUN_cec1();       /* payload[7]  = word_0109 lo           */
  FUN_ceb8(local_b); FUN_cec1();       /* payload[8]                            */
  FUN_ceb8(local_b); FUN_cec1();       /* payload[9]  = word_010b              */
  if (*(char *)(_uStack_1 + 8) != '\0') { FUN_ceb8(local_b); FUN_cec1(); }
  FUN_ceb8(local_b); FUN_cec1();
  local_6 = 0;
  while( true ) {                      /* variable tail: record[0xb] more bytes */
    if (*(byte *)(_uStack_1 + 0xb) <= local_6) break;
    FUN_ceb8(local_b); FUN_cec1();
    local_6 = local_6 + 1;
  }
  FUN_8ec7(uStack_8);                  /* CRC hi (payload trailer byte 0) */
  FUN_8ec7(uStack_7);                  /* CRC lo (payload trailer byte 1) */
  return;
}

/* ---- FUN_e088 @ e088 : seed the CRC accumulator to 0xFFFF ------------------ */
/* aka crc16_seed. */
void FUN_e088(undefined1 *param_1) { *param_1 = 0xff; param_1[1] = 0xff; return; }
