/*
 * Ghidra decompiler output — I2C / M24128 (U3) EEPROM access layer.
 *
 * Auto-generated, NOT hand-written or compilable. Reproduce with:
 *   tools/analyze.sh DecompileCallers.java eeprom_i2c.c a554 ded1 dfd2 df6a
 *
 * FUN_a554(buf,_,len,addrHi,addrLo) = generic sequential read of `len` bytes
 * from a 16-bit word address; FUN_ded1 = I2C start + send byte; FUN_dfd2 =
 * read byte; FUN_df6a = stop. The FUN_a6xx / FUN_b5xx wrappers just call
 * FUN_a554 with a fixed length (2/4/5/16/23/25). `undefinedN` / `in_stack_*`
 * artifacts are the decompiler's view of HCS08 stacked args, not real types.
 */

########## FUN_a554 @ a554 ##########

/* WARNING: Instruction at (RAM,0xa575) overlaps instruction at (RAM,0xa574)
    */

undefined1 FUN_a554(undefined2 param_1,short param_2)

{
  byte bVar1;
  undefined1 *puVar2;
  char cVar3;
  undefined1 uVar4;
  char cVar6;
  short sVar5;
  undefined1 in_stack_00000000;
  char cStack_1;
  undefined1 *puVar7;
  
  cVar6 = (char)param_1;
  puVar7 = (undefined1 *)CONCAT11((char)((ushort)param_1 >> 8),in_stack_00000000);
  cVar3 = func_0xa510();
  if (cVar3 != '\0') {
    FUN_df6a();
    cVar3 = FUN_ded1();
    if (cVar3 != '\0') {
      while (puVar2 = puVar7, sVar5 = param_2 + -1, param_2 != 0) {
        if (sVar5 == 0) {
          bVar1 = IIC1C;
          IIC1C = bVar1 | 8;
        }
        bVar1 = IIC1C;
        IIC1C = bVar1 & 0xf7;
        uVar4 = FUN_dfd2();
        *puVar7 = uVar4;
        cVar6 = cVar6 + '\x01';
        if (cVar6 == '\0') {
          cStack_1 = (char)((ushort)puVar7 >> 8);
          puVar7 = (undefined1 *)CONCAT11(cStack_1 + '\x01',(char)puVar2);
        }
        DAT_012c = DAT_012c + '\x01';
        param_2 = sVar5;
        if (DAT_012c == '\0') {
          DAT_012b = DAT_012b + '\x01';
        }
      }
      FUN_df6a();
      return 1;
    }
  }
  return 0;
}



########## FUN_a631 @ a631 ##########

void FUN_a631(undefined2 param_1)

{
  undefined1 in_stack_00000000;
  
  FUN_a554(in_stack_00000000,0,2,(char)((ushort)param_1 >> 8),(char)param_1);
  return;
}



########## FUN_a646 @ a646 ##########

void FUN_a646(undefined2 param_1)

{
  undefined1 in_stack_00000000;
  
  FUN_a554(in_stack_00000000,0,4,(char)((ushort)param_1 >> 8),(char)param_1);
  return;
}



########## FUN_b596 @ b596 ##########

void FUN_b596(undefined2 param_1)

{
  undefined1 in_stack_00000000;
  
  FUN_a554(in_stack_00000000,0,5,(char)((ushort)param_1 >> 8),(char)param_1);
  return;
}



########## FUN_b5ab @ b5ab ##########

void FUN_b5ab(undefined2 param_1)

{
  undefined1 in_stack_00000000;
  
  FUN_a554(in_stack_00000000,0,0x17,(char)((ushort)param_1 >> 8),(char)param_1);
  return;
}



########## FUN_b5d5 @ b5d5 ##########

void FUN_b5d5(undefined2 param_1)

{
  undefined1 in_stack_00000000;
  
  FUN_a554(in_stack_00000000,0,1,(char)((ushort)param_1 >> 8),(char)param_1);
  return;
}



########## FUN_b5ea @ b5ea ##########

void FUN_b5ea(undefined2 param_1)

{
  undefined1 in_stack_00000000;
  
  if ((char)((char)param_1 + '\x01') == '\0') {
    param_1._0_1_ = param_1._0_1_ + '\x01';
  }
  FUN_a554(in_stack_00000000,0,4,param_1._0_1_,(char)param_1 + '\x01');
  return;
}



########## FUN_b76f @ b76f ##########

void FUN_b76f(undefined2 param_1,short param_2,undefined2 param_3)

{
  ushort uVar1;
  undefined1 uVar2;
  ushort uVar3;
  undefined1 in_stack_00000000;
  undefined1 uStack_f;
  undefined2 local_e;
  byte bStack_c;
  short local_b;
  undefined1 auStack_9 [8];
  undefined1 uStack_1;
  
  uVar3 = CONCAT11((char)((ushort)param_1 >> 8),in_stack_00000000);
  FUN_e088();
  local_b = param_2;
  for (; uVar1 = uVar3, uVar2 = (undefined1)((ushort)local_b >> 8), 8 < uVar3; uVar3 = uVar3 - 8) {
    FUN_a554(auStack_9,0,8,uVar2,(char)local_b);
    bStack_c = 0;
    do {
      local_e = param_3;
      FUN_b802(&uStack_f);
      FUN_e0cd();
      bStack_c = bStack_c + 1;
    } while (bStack_c < 8);
    local_b = local_b + 8;
  }
  if (uVar3 != 0) {
    uStack_1 = (undefined1)(uVar3 >> 8);
    FUN_a554(auStack_9,uStack_1,(char)param_1,uVar2,(char)local_b);
    uVar3 = uVar1;
    for (bStack_c = 0; bStack_c < uVar3; bStack_c = bStack_c + 1) {
      local_e = param_3;
      FUN_b802(&uStack_f);
      FUN_e0cd();
    }
  }
  return;
}



########## FUN_b5c0 @ b5c0 ##########

void FUN_b5c0(undefined2 param_1)

{
  undefined1 in_stack_00000000;
  
  FUN_a554(in_stack_00000000,0,0x19,(char)((ushort)param_1 >> 8),(char)param_1);
  return;
}



########## FUN_a61c @ a61c ##########

void FUN_a61c(undefined2 param_1)

{
  undefined1 in_stack_00000000;
  
  FUN_a554(in_stack_00000000,0,1,(char)((ushort)param_1 >> 8),(char)param_1);
  return;
}



########## FUN_bb8e @ bb8e ##########

void FUN_bb8e(void)

{
  undefined1 in_stack_00000000;
  
  FUN_bca8();
  FUN_a554(in_stack_00000000,0,5,0x3a,0x9d);
  return;
}



########## FUN_bbe8 @ bbe8 ##########

void FUN_bbe8(undefined2 param_1)

{
  undefined1 in_stack_00000000;
  
  FUN_bca8();
  FUN_a554(in_stack_00000000,0,0x10,(char)((ushort)param_1 >> 8),(char)param_1);
  return;
}



########## FUN_dd9c @ dd9c ##########

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

void FUN_dd9c(void)

{
  if (_DAT_06ea == 0) {
    if (_DAT_06ec != 0) {
      FUN_a554(_DAT_069b,DAT_06ec,DAT_06ed);
      FUN_de08();
      FUN_de12(DAT_06ed);
      DAT_0142 = 0;
      _DAT_06ec = 0;
      _DAT_06ea = 0;
    }
  }
  else {
    FUN_a554(0,0x11);
    _DAT_06ef = _DAT_06ef + 0x11;
    FUN_de08();
    FUN_de12();
    if (DAT_06eb == '\0') {
      _DAT_06ea = (ushort)(byte)(DAT_06ea - 1) << 8;
    }
    _DAT_06ea = CONCAT11(DAT_06ea,DAT_06eb + -1);
    if ((_DAT_06ea == 0) && (_DAT_06ec == 0)) {
      DAT_0142 = 0;
      return;
    }
  }
  return;
}



########## FUN_ded1 @ ded1 ##########

/* WARNING: Instruction at (RAM,0xdf3e) overlaps instruction at (RAM,0xdf3d)
    */
/* WARNING: Possible PIC construction at 0xdf3e: Changing call to branch */
/* WARNING: Possible PIC construction at 0xdf1c: Changing call to branch */

undefined1 FUN_ded1(undefined1 param_1)

{
  char extraout_A;
  char cVar1;
  char extraout_A_00;
  char extraout_A_01;
  undefined1 uVar2;
  short sVar3;
  undefined1 *puVar4;
  byte bVar5;
  byte bVar6;
  undefined1 uStack_b;
  undefined2 uStack_a;
  undefined1 local_4 [4];
  
  bVar5 = IIC1C;
  IIC1C = bVar5 | 0x10;
  bVar5 = IIC1C;
  IIC1C = bVar5 & 0xdf;
  bVar5 = 2;
  uStack_a = (undefined *)0xdedf;
  FUN_912a(0,local_4);
  uStack_a = (undefined *)0xdee5;
  sVar3 = FUN_9d8e(local_4);
  bVar6 = PTAD;
  bVar5 = bVar5 & 0xfe;
  if ((bVar6 & 1) == 0) {
                    /* WARNING: Subroutine does not return */
    uStack_a = &UNK_deee;
    FUN_9e26(extraout_A - *(char *)(sVar3 + -0x5004));
  }
  cVar1 = extraout_A;
  puVar4 = (undefined1 *)((short)&uStack_a + 1);
  if ((DAT_00ad & 8) != 0) {
    while( true ) {
      if ((bVar5 >> 1 & 1) == 0) {
        return 0;
      }
      SRS = cVar1;
      bVar5 = (cVar1 == '\0') << 1;
      bVar6 = IIC1S;
      if ((bVar6 & 0x20) == 0) break;
      uStack_a = (undefined *)0xdef2;
      cVar1 = func_0xdf59();
    }
    bVar5 = IIC1S;
    IIC1S = bVar5 | 0x10;
    bVar5 = IIC1C;
    IIC1C = bVar5 | 0x10;
    bVar5 = IIC1C;
    IIC1C = bVar5 | 0x20;
    IIC1D = param_1;
    bVar6 = 2;
    uStack_a = (undefined *)0xdf0b;
    FUN_912a(0,local_4);
    uStack_a = (undefined *)0xdf11;
    sVar3 = FUN_9d8e(local_4);
    bVar5 = PTAD;
    if ((bVar5 & 1) == 0) {
                    /* WARNING: Subroutine does not return */
      uStack_a = &UNK_df1a;
      FUN_9e26(extraout_A_00 - *(char *)(sVar3 + -0x5004));
    }
    puVar4 = (undefined1 *)((short)&uStack_a + 1);
    if ((DAT_00ad & 8) != 0) {
      if ((bVar6 >> 1 & 1) == 0) {
        return 0;
      }
      SRS = extraout_A_00;
      bVar5 = IIC1S;
      if ((bVar5 & 0x80) == 0) {
        bVar6 = 2;
        uStack_a = (undefined *)0xdf2d;
        FUN_912a(0,local_4);
        uStack_a = (undefined *)0xdf33;
        sVar3 = FUN_9d8e(local_4);
        bVar5 = PTAD;
        if ((bVar5 & 1) == 0) {
                    /* WARNING: Subroutine does not return */
          uStack_a = &UNK_df3c;
          FUN_9e26(extraout_A_01 - *(char *)(sVar3 + -0x5004));
        }
        puVar4 = (undefined1 *)((short)&uStack_a + 1);
        if ((DAT_00ad & 8) != 0) {
          if ((bVar6 >> 1 & 1) == 0) {
            return 0;
          }
          SRS = extraout_A_01;
          bVar5 = IIC1S;
          if ((bVar5 & 0x80) != 0) {
            bVar5 = IIC1S;
            if (((bVar5 & 0x10) == 0) && (bVar5 = IIC1S, (bVar5 & 1) == 0)) {
              return 1;
            }
            uStack_a = (undefined *)0xdf55;
            FUN_df6a();
            return 0;
          }
          uStack_a = (undefined *)0xdf40;
          puVar4 = &uStack_b;
        }
      }
      else {
        uStack_a = (undefined *)0xdf1e;
        puVar4 = &uStack_b;
      }
    }
  }
  *puVar4 = (char)*(undefined2 *)(puVar4 + 5);
  puVar4[-1] = (char)((ushort)*(undefined2 *)(puVar4 + 5) >> 8);
  puVar4[-2] = (char)*(undefined2 *)(puVar4 + 3);
  puVar4[-3] = (char)((ushort)*(undefined2 *)(puVar4 + 3) >> 8);
  *(undefined2 *)(puVar4 + -5) = 0xdf66;
  uVar2 = FUN_e03a();
  return uVar2;
}



########## FUN_a4f0 @ a4f0 ##########

/* WARNING: Globals starting with '_' overlap smaller symbols at the same address */

undefined1 FUN_a4f0(void)

{
  char cVar1;
  undefined1 uVar2;
  
  cVar1 = FUN_ded1();
  uVar2 = 0;
  if (cVar1 != '\0') {
    FUN_df6a();
    cVar1 = func_0xa510(0);
    if (cVar1 == '\0') {
      return 0;
    }
    FUN_df6a();
    _DAT_012b = 0;
    uVar2 = 1;
  }
  return uVar2;
}



########## FUN_dfd2 @ dfd2 ##########

/* WARNING: Instruction at (RAM,0xe014) overlaps instruction at (RAM,0xe013)
    */
/* WARNING: Possible PIC construction at 0xe014: Changing call to branch */
/* WARNING: Possible PIC construction at 0xdff2: Changing call to branch */

undefined1 FUN_dfd2(void)

{
  byte bVar1;
  char extraout_A;
  char extraout_A_00;
  undefined1 uVar2;
  short sVar3;
  undefined1 *puVar4;
  byte bVar5;
  undefined1 uStack_b;
  undefined2 uStack_a;
  undefined1 local_4 [4];
  
  bVar1 = IIC1C;
  IIC1C = bVar1 & 0xef;
  uVar2 = IIC1D;
  bVar5 = 2;
  uStack_a = (undefined *)0xdfe1;
  FUN_912a(0,local_4);
  uStack_a = (undefined *)0xdfe7;
  sVar3 = FUN_9d8e(local_4);
  puVar4 = (undefined1 *)((short)&uStack_a + 1);
  bVar1 = PTAD;
  if ((bVar1 & 1) == 0) {
                    /* WARNING: Subroutine does not return */
    uStack_a = &UNK_dff0;
    FUN_9e26(extraout_A - *(char *)(sVar3 + -0x5004));
  }
  if ((DAT_00ad & 8) != 0) {
    if ((bVar5 >> 1 & 1) == 0) {
      return 0xff;
    }
    SRS = extraout_A;
    bVar1 = IIC1S;
    if ((bVar1 & 0x80) == 0) {
      bVar5 = 2;
      uStack_a = (undefined *)0xe003;
      FUN_912a(0,local_4);
      uStack_a = (undefined *)0xe009;
      sVar3 = FUN_9d8e(local_4);
      puVar4 = (undefined1 *)((short)&uStack_a + 1);
      bVar1 = PTAD;
      if ((bVar1 & 1) == 0) {
                    /* WARNING: Subroutine does not return */
        uStack_a = &UNK_e012;
        FUN_9e26(extraout_A_00 - *(char *)(sVar3 + -0x5004));
      }
      if ((DAT_00ad & 0x20) != 0) {
        if ((bVar5 >> 1 & 1) != 1) {
          return 0xff;
        }
        SRS = extraout_A_00;
        bVar1 = IIC1S;
        if ((bVar1 & 0x80) != 0) {
          bVar1 = IIC1C;
          IIC1C = bVar1 | 0x10;
          uVar2 = IIC1D;
          return uVar2;
        }
        uStack_a = (undefined *)0xe016;
        puVar4 = &uStack_b;
      }
    }
    else {
      uStack_a = (undefined *)0xdff4;
      puVar4 = &uStack_b;
    }
  }
  *puVar4 = (char)*(undefined2 *)(puVar4 + 5);
  puVar4[-1] = (char)((ushort)*(undefined2 *)(puVar4 + 5) >> 8);
  puVar4[-2] = (char)*(undefined2 *)(puVar4 + 3);
  puVar4[-3] = (char)((ushort)*(undefined2 *)(puVar4 + 3) >> 8);
  *(undefined2 *)(puVar4 + -5) = 0xe036;
  uVar2 = FUN_e03a();
  return uVar2;
}



########## FUN_df6a @ df6a ##########

void FUN_df6a(void)

{
  byte bVar1;
  
  bVar1 = IIC1C;
  IIC1C = bVar1 | 0x10;
  bVar1 = IIC1C;
  IIC1C = bVar1 & 0xdf;
  return;
}



########## FUN_a5c9 @ a5c9 ##########

undefined1 FUN_a5c9(short param_1,byte param_2)

{
  char cVar1;
  short sVar2;
  
  while (cVar1 = func_0xa510(), cVar1 != '\0') {
    do {
      sVar2 = param_1 + -1;
      if (param_1 == 0) {
        FUN_df6a();
        return 1;
      }
      cVar1 = FUN_df6f();
      if (cVar1 == '\0') goto LAB_a5d4;
      DAT_012c = DAT_012c + '\x01';
      if (DAT_012c == '\0') {
        DAT_012b = DAT_012b + '\x01';
      }
      param_2 = param_2 + 1;
      param_1 = sVar2;
    } while ((param_2 & 0x3f) != 0);
    FUN_df6a();
  }
LAB_a5d4:
  FUN_df6a();
  return 0;
}



########## FUN_a5a5 @ a5a5 ##########

undefined1 FUN_a5a5(void)

{
  char cVar1;
  
  cVar1 = func_0xa510();
  if (cVar1 != '\0') {
    cVar1 = FUN_df6f();
    if (cVar1 != '\0') {
      DAT_012c = DAT_012c + '\x01';
      if (DAT_012c == '\0') {
        DAT_012b = DAT_012b + '\x01';
      }
      FUN_df6a();
      return 1;
    }
  }
  FUN_df6a();
  return 0;
}



