/*
 * MC9S08GT (U2) — alarm/status TX, Ghidra decompile (raw, lightly annotated).
 *
 * The radio receiver is the indoor infopanel: 5 alarm symbols + one OK button
 * + a green/red LED. Each radio message carries a TYPE byte and a boolean state.
 * There are exactly 5 message types -> the 5 panel symbols:
 *
 *   type  RAM source(s)                 alarm code(s)     panel meaning
 *   0x20  DAT_0757                       0x02 (E011)       status / OK LED
 *   0x21  DAT_06ff                       0x15 (E021)       chemical level low
 *   0x22  DAT_0707/070f/075b + array     0x1F/0x20 (E03x)  high water
 *   0x23  DAT_074f                       0x33 (E051)       sludge-empty reminder
 *   0x26  DAT_0747 + array 0x0717..      0x2F,0x28-0x2D    device fault "!"
 *
 * FUN_e372 (aka alarm_reset_broadcast_all) = reset/all-clear: sends all 5
 *   types with state 0. Runs at boot AND on the styrskåp's own green Test
 *   button's 10-14s long-press (see docs/ghidra/codes.md §5) - same routine.
 * FUN_e427 (aka alarm_broadcast_current_state) = periodic: sends each type
 *   with its live boolean.
 * FUN_e0e5 (aka alarm_change_detector) = event-driven: sends a type ONLY when
 *   its state changed (edge).
 *
 * Alarm codes -> E-codes are in docs/eeprom-map.md. Because live alarms are
 * edge-driven (FUN_e0e5), an idle capture shows only the two heartbeat frames;
 * to see the 5 alarm frames, power-cycle the styrskap during capture (FUN_e372).
 */

/* ---- FUN_e372 @ e372 : reset -> emit all 5 symbols as "clear" -------------- */
/* aka alarm_reset_broadcast_all(param_1). param_1 distinguishes boot (0) from */
/* the Test-button long-press (1) - only affects one EEPROM-error check below, */
/* the alarm-table clear + 5-message broadcast is identical either way.       */
void FUN_e372(char param_1)
{
  undefined1 extraout_HI, extraout_HI_00, extraout_HI_01, extraout_HI_02;
  char in_X;
  byte bVar2;
  ushort uVar1;
  byte bStack_2;

  if (param_1 == '\0') _DAT_0134 = (ushort)(DAT_0135 & 0x40);
  else                 _DAT_0134 = 0;

  bStack_2 = 0;                         /* clear the 12-entry x 8-byte alarm table @06f7 */
  do {
    bVar2 = bStack_2 << 3;
    uVar1 = (ushort)bVar2;
    (&DAT_06f7)[uVar1] = 0;
    *(undefined1 *)(uVar1 + 0x6f8) = 0;
    *(undefined1 *)(uVar1 + 0x6f9) = 0;
    if (in_X != '\0') {
      FUN_a66e(0,(char)((ushort)*(undefined2 *)(&DAT_06fc + uVar1) >> 8),
               (char)*(undefined2 *)(&DAT_06fc + uVar1));
      *(undefined1 *)(bVar2 + 0x6fb) = 0;
      *(undefined1 *)(bVar2 + 0x6fa) = 0;
    }
    bStack_2 = bStack_2 + 1;
  } while (bStack_2 < 0xc);

  bStack_2 = 0;                         /* clear the 2 x status slots @0757 */
  do {
    uVar1 = (ushort)(byte)(bStack_2 << 2);
    (&DAT_0757)[uVar1] = 0;
    (&DAT_0758)[uVar1] = 0;
    *(undefined1 *)(uVar1 + 0x759) = 0;
    bStack_2 = bStack_2 + 1;
  } while (bStack_2 < 2);

  if ((_DAT_0134 & 0x40) != 0) DAT_0747 = 0x2f;   /* EEPROM/program fault seed */
  if (_DAT_0134 == 0) { FUN_a334(); FUN_a1d3(); }
  DAT_060f = 0; DAT_0610 = 0; DAT_0611 = 0; DAT_0612 = 0;

  FUN_db22(0x20);                                        /* status / OK LED     */
  FUN_db22(0,CONCAT11(extraout_HI,   0x21));             /* chemical low        */
  FUN_db22(0,CONCAT11(extraout_HI_00,0x22));             /* high water          */
  FUN_db22(0,CONCAT11(extraout_HI_01,0x23));             /* sludge reminder     */
  FUN_db22(0,CONCAT11(extraout_HI_02,0x26));             /* device fault "!"    */
  return;
}

/* ---- FUN_e427 @ e427 : periodic -> emit all 5 with their live booleans ----- */
/* aka alarm_broadcast_current_state. Calls FUN_de1b (aka                    */
/* send_heartbeat_poll_and_wait, in mc9s08gt32_full.c) first - only proceeds  */
/* if that poll/ack round-trip succeeded.                                    */
void FUN_e427(void)
{
  bool bVar1;
  char cVar2;
  undefined1 uVar3;
  char cStack_2;
  byte bStack_1;

  bVar1 = false;
  cStack_2 = '\0';
  cVar2 = FUN_de1b((char)((ushort)_DAT_013e >> 8),(char)_DAT_013e,
                   (char)((ushort)_DAT_0140 >> 8),(char)_DAT_0140);
  if (cVar2 != '\0') {
    FUN_db22(DAT_0757 != '\0');                          /* 0x20 status         */
    FUN_db22(DAT_06ff != '\0');                          /* 0x21 chemical low   */
    if (DAT_0707 != '\0') cStack_2 = '\x01';
    if (DAT_070f != '\0') cStack_2 = '\x01';
    if ((DAT_075b == '\0') && (cStack_2 == '\0')) uVar3 = 0; else uVar3 = 1;
    FUN_db22(uVar3);                                     /* 0x22 high water     */
    FUN_db22(DAT_074f != '\0');                          /* 0x23 sludge remind  */
    bStack_1 = 0;                                        /* scan MV/compressor  */
    do {
      if (*(char *)((byte)(bStack_1 << 3) + 0x717) != '\0') bVar1 = true;
      bStack_1 = bStack_1 + 1;
    } while (bStack_1 < 6);
    if ((DAT_0747 != '\0') || (bVar1)) uVar3 = 1; else uVar3 = 0;
    FUN_db22(uVar3,CONCAT11((char)((ushort)&cStack_2 >> 8),0x22));   /* 0x26 fault */
  }
  return;
}

/* ---- FUN_e0e5 @ e0e5 : edge-driven -> emit a type only when its state moved  */
/* aka alarm_change_detector. Each block compares a "last-sent" shadow          */
/* (0758/0700/06f8.../075c/0750/0748) against the live value; on a change it    */
/* updates the shadow and calls FUN_db22 (aka queue_radio_message) with the     */
/* new boolean. This is why alarms are NOT in the idle heartbeat.               */
void FUN_e0e5(char param_1,char param_2)
{
  bool bVar1;
  undefined1 *puVar2;
  char *pcVar3;
  char cVar5;
  byte extraout_X, extraout_X_00, extraout_X_01;
  short sVar4;
  byte bVar6, unaff_retaddr, unaff_retaddr_00, bVar7;
  undefined1 auStack_3 [3];

  if (_DAT_0134 == 0) FUN_a334(); else FUN_a32f();
  puVar2 = (undefined1 *)FUN_9da6();
  if ((DAT_00ff & 1) != 0) *puVar2 = (char)puVar2;
  *puVar2 = (char)puVar2;
  pcVar3 = (char *)FUN_9df0(auStack_3);
  cVar5 = (char)pcVar3;
  if ((DAT_00ff & 1) != 0) *pcVar3 = cVar5;
  *pcVar3 = cVar5;
  if (cVar5 == '\0') { bVar7 = 2; }
  else {
    bVar7 = (((DAT_0141 == '\0' && DAT_0140 == '\0') && DAT_013f == '\0') && DAT_013e == '\0') << 1;
    if (((DAT_0141 != '\0' || DAT_0140 != '\0') || DAT_013f != '\0') || DAT_013e != '\0') {
      if (DAT_013d != '\0') {
        bVar7 = (DAT_0758 == DAT_0757) << 1;                     /* 0x20 status  */
        if ((DAT_0758 != DAT_0757) && (FUN_e26f(), (bVar7 >> 1 & 1) != 1)) {
          DAT_0758 = DAT_0757; FUN_db22(DAT_0757 != '\0'); }
        bVar7 = (DAT_0700 == DAT_06ff) << 1;                     /* 0x21 chem    */
        if ((DAT_0700 != DAT_06ff) && (FUN_e26f(), (bVar7 >> 1 & 1) != 1)) {
          DAT_0700 = DAT_06ff; FUN_db22(DAT_06ff != '\0'); }
        bVar7 = 2;
        do {                                                     /* high-water array */
          FUN_e280();
          bVar1 = *(char *)(extraout_X + 0x6f8) == (&DAT_06f7)[extraout_X];
          bVar6 = bVar1 << 1;
          if ((!bVar1) && (FUN_e26f(), (bVar6 >> 1 & 1) != 1))
            *(undefined1 *)(extraout_X + 0x6f8) = (&DAT_06f7)[extraout_X];
          FUN_e280();
          if ((&DAT_06f7)[extraout_X_00] != '\0') param_2 = '\x01';
          bVar7 = bVar7 + 1;
        } while (bVar7 < 4);
        bVar7 = (DAT_075c == DAT_075b) << 1;
        if ((DAT_075c != DAT_075b) && (FUN_e26f(), (bVar7 >> 1 & 1) != 1)) DAT_075c = DAT_075b;
        if (DAT_075b != '\0') param_2 = '\x01';
        bVar7 = (DAT_0149 == param_2) << 1;                      /* 0x22 high water */
        if ((DAT_0149 != param_2) && (FUN_e26f(), (bVar7 >> 1 & 1) != 1)) {
          DAT_0149 = param_2; FUN_db22(param_2 != '\0'); }
        bVar7 = (DAT_0750 == DAT_074f) << 1;                     /* 0x23 sludge  */
        if ((DAT_0750 != DAT_074f) && (FUN_e26f(), (bVar7 >> 1 & 1) != 1)) {
          DAT_0750 = DAT_074f; FUN_db22(DAT_074f != '\0'); }
        unaff_retaddr_00 = 0;
        goto LAB_e206;
      }
      bVar7 = 2;
    }
  }
  do {
    if ((bVar7 >> 1 & 1) == 1) return;
    *(undefined1 *)(unaff_retaddr + 0x718) = *(undefined1 *)(unaff_retaddr + 0x717);
    do {
      sVar4 = FUN_e280();
      if (*(char *)(sVar4 + 0x717) != '\0') param_1 = '\x01';
      unaff_retaddr_00 = unaff_retaddr_00 + 1;
      if (5 < unaff_retaddr_00) {                                /* 0x26 device fault */
        if (DAT_0748 != DAT_0747) DAT_0748 = DAT_0747;
        if (DAT_0747 != '\0') param_1 = '\x01';
        bVar7 = (DAT_0148 == param_1) << 1;
        if (DAT_0148 == param_1) return;
        FUN_e26f();
        if ((bVar7 >> 1 & 1) == 1) return;
        DAT_0148 = param_1;
        FUN_db22(param_1 != '\0',CONCAT11((char)((ushort)&stack0x0001 >> 8),0x22));
        return;
      }
LAB_e206:
      FUN_e280();
      bVar1 = *(char *)(extraout_X_01 + 0x718) == *(char *)(extraout_X_01 + 0x717);
      bVar7 = bVar1 << 1;
    } while (bVar1);
    FUN_e26f();
    unaff_retaddr = extraout_X_01;
  } while( true );
}
