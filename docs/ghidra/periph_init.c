/*
 * Ghidra decompiler output — peripheral init (runs from reset).
 *
 * Auto-generated, NOT hand-written or compilable. Reproduce with:
 *   tools/analyze.sh DecompileCallers.java periph_init.c fe80
 *
 * Key line for the radio question: `SPI1C1 = 0xcc;` sets MSTR=0, so the
 * MC9S08 is the SPI *slave* — it does not drive the nRF905 config. Also sets
 * the I2C registers (IIC1A/IIC1F/IIC1C) and SCI/clock/port directions.
 */

########## FUN_fe80 @ fe80 ##########

undefined2 FUN_fe80(void)

{
  byte bVar1;
  undefined1 uVar2;
  
  SOPT = 0xd1;
  SPMSC1 = 0x1c;
  SPMSC2 = 0;
  ICGC1 = 0x3c;
  ICGC2 = 0x70;
  while (bVar1 = ICGS1, (bVar1 & 8) == 0) {
    SRS = 0;
  }
  PTASE = 0;
  PTBSE = 0;
  PTCSE = 0;
  bVar1 = PTDSE;
  PTDSE = bVar1 & 0xe0;
  bVar1 = PTESE;
  PTESE = bVar1 & 0xc0;
  bVar1 = PTGSE;
  PTGSE = bVar1 & 0xf0;
  TPM1SC = 0;
  RAM0033 = 0x10;
  uVar2 = TPM1SC;
  TPM1SC = 0x50;
  bVar1 = PTDD;
  PTDD = bVar1 & 0xe4;
  bVar1 = PTDPE;
  PTDPE = bVar1 & 0xfe;
  bVar1 = PTDDD;
  PTDDD = bVar1 | 0x1b;
  IIC1A = 6;
  IIC1F = 0x2f;
  IIC1S = 0x12;
  bVar1 = IIC1C;
  IIC1C = bVar1 | 0x80;
  PTAD = 0;
  PTAPE = 0;
  PTADD = 0xff;
  SCI1C2 = 0;
  uVar2 = SCI1S1;
  uVar2 = SCI1D;
  SCI1BDH = 0;
  SCI1BDL = 0x7b;
  SCI1C1 = 0;
  SCI1C3 = 0;
  SCI1C2 = 0x2c;
  SPI1C1 = 0;
  SPI1C2 = 0;
  SPI1BR = 0x23;
  uVar2 = SPI1S;
  SPI1C1 = 0xcc;
  bVar1 = PTBPE;
  PTBPE = bVar1 | 1;
  bVar1 = PTBDD;
  PTBDD = bVar1 & 0xfe | 0xde;
  bVar1 = PTCD;
  PTCD = bVar1 | 0x50;
  bVar1 = PTCPE;
  PTCPE = bVar1 & 0x8f;
  bVar1 = PTCDD;
  PTCDD = bVar1 & 0xdf | 0x50;
  SCI2C2 = 0;
  uVar2 = SCI2S1;
  uVar2 = SCI2D;
  SCI2BDH = 0;
  SCI2BDL = 0x7b;
  SCI2C1 = 0;
  SCI2C3 = 0x20;
  SCI2C2 = 0x2c;
  bVar1 = IRQSC;
  IRQSC = bVar1 & 0xfd;
  bVar1 = IRQSC;
  IRQSC = bVar1 | 0x10;
  bVar1 = IRQSC;
  IRQSC = bVar1 | 4;
  SRS = 0xff;
  FSTAT = 0x30;
  FCDIV = 0x4d;
  return 0x10;
}



########## FUN_92fb @ 92fb ##########

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_92fb(void)

{
  byte bVar1;
  undefined1 uVar2;
  char cVar3;
  
  FUN_fe80();
  FUN_92e8();
  FUN_bdd8();
  FUN_8fc4(0,0xff,0xff);
  uVar2 = FUN_a728();
  SRS = uVar2;
  FUN_bccc();
  if (DAT_0606 == '\0') {
    DAT_05ec = DAT_0606;
    FUN_a460();
  }
  else {
    if (DAT_06e9 == '\x01') {
      DAT_05ec = DAT_06e9;
      DAT_0111 = 1;
    }
    bVar1 = SCI1C2;
    SCI1C2 = bVar1 | 8;
    bVar1 = SCI1C2;
    SCI1C2 = bVar1 | 4;
  }
  FUN_d017();
  FUN_d007();
  FUN_caca();
  cVar3 = FUN_a489();
  if (cVar3 != '\0') {
    FUN_de96();
    DAT_05ec = '\x01';
    DAT_0111 = 1;
    goto LAB_9382;
  }
  if (DAT_06e9 == '\0') {
LAB_9378:
    if (DAT_06e9 != '\0') goto LAB_9382;
  }
  else {
    cVar3 = FUN_de1b((char)((ushort)_DAT_013e >> 8),(char)_DAT_013e);
    if (cVar3 == '\0') goto LAB_9378;
  }
  FUN_a460();
LAB_9382:
  do {
    if (DAT_0150 == '\0') {
      if (DAT_014b == '\0') {
        SRS = 0;
        uVar2 = FUN_c03c();
        SRS = uVar2;
        uVar2 = FUN_97cd();
        SRS = uVar2;
        uVar2 = FUN_a339();
        SRS = uVar2;
        uVar2 = FUN_d02f();
        SRS = uVar2;
        uVar2 = 0;
        if (DAT_014a != '\0') {
          uVar2 = FUN_93f8();
        }
        SRS = uVar2;
        cVar3 = DAT_0139;
        if (DAT_0139 == '\0') {
          FUN_e0e5();
          cVar3 = FUN_e32c();
        }
        SRS = cVar3;
        uVar2 = FUN_dbb3();
        SRS = uVar2;
        FUN_fc7c();
      }
      else {
        SRS = DAT_014b;
        FUN_e4d0();
        FUN_e525();
        FUN_d02f();
        if (DAT_014a != '\0') {
          FUN_93f8();
        }
      }
    }
    else {
      SRS = DAT_0150;
      FUN_e541();
      FUN_e5eb();
    }
  } while (_DAT_075f == -0x55ab);
  FUN_94e9(0x29a);
  do {
                    /* WARNING: Do nothing block with infinite loop */
  } while( true );
}



