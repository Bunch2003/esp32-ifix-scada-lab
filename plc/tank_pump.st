(*
  Tank T-100 / Pump P-101 control program
  Target: ESP32 running OpenPLC (IEC 61131-3 Structured Text)
  Spec:   docs/control-narrative.md
  Tags:   docs/io-map.md

  Before uploading, confirm in OpenPLC Editor that every %IX / %QX / %IW
  address below exists for the ESP32 board and is wired to the GPIO you expect.
*)

PROGRAM TankPump
  VAR
    (* ---------- Physical inputs ---------- *)
    HS_101A_Start   AT %IX0.0 : BOOL;   (* local START, pressed = TRUE *)
    HS_101B_Stop    AT %IX0.1 : BOOL;   (* local STOP, pressed = TRUE *)
    LSHH_100        AT %IX0.2 : BOOL;   (* high-high float switch, active = TRUE *)
    LT_100_Raw      AT %IW0   : UINT;   (* level pot, raw analog counts *)

    (* ---------- Physical outputs ---------- *)
    P_101_Run       AT %QX0.0 : BOOL;   (* pump *)
    XV_101_Open     AT %QX0.1 : BOOL;   (* inlet valve *)
    XA_100_Horn     AT %QX0.2 : BOOL;   (* alarm horn *)

    (* ---------- SCADA commands (Modbus coils, written by iFIX) ---------- *)
    HMI_Start       AT %QX1.0 : BOOL;   (* momentary: PLC clears it *)
    HMI_Stop        AT %QX1.1 : BOOL;   (* momentary: PLC clears it *)
    HMI_AutoMode    AT %QX1.2 : BOOL;   (* maintained: TRUE = Auto *)
    HMI_ValveOpen   AT %QX1.3 : BOOL;   (* maintained: TRUE = open requested *)
    HMI_AlarmAck    AT %QX1.4 : BOOL;   (* momentary: PLC clears it *)

    (* ---------- Status to SCADA (Modbus coils, read by iFIX) ---------- *)
    LAH_100         AT %QX1.5 : BOOL;   (* high level alarm *)
    LAL_100         AT %QX1.6 : BOOL;   (* low level alarm *)
    P_101_Fault     AT %QX1.7 : BOOL;   (* pump max-run-time fault, latched *)

    (* ---------- Registers (Modbus holding registers) ---------- *)
    LT_100_Pct      AT %MW0 : UINT;     (* level 0-100 % *)
    SP_PumpOn       AT %MW1 : UINT;     (* % *)
    SP_PumpOff      AT %MW2 : UINT;     (* % *)
    SP_HighAlarm    AT %MW3 : UINT;     (* % *)
    SP_LowAlarm     AT %MW4 : UINT;     (* % *)
    P_101_Starts    AT %MW5 : UINT;     (* start counter *)
    P_101_RunSec    AT %MW6 : UINT;     (* total run time, seconds *)
    PLC_Heartbeat   AT %MW7 : UINT;     (* +1 every scan; SCADA detects stale comms *)
  END_VAR

  VAR
    RunLatch        : BOOL;
    StartRequest    : BOOL;
    StopRequest     : BOOL;
    AnyAlarm        : BOOL;
    HornLatch       : BOOL;
    StartEdge       : R_TRIG;           (* counts pump starts *)
    AlarmEdge       : R_TRIG;           (* sounds horn on a new alarm *)
    MaxRunTimer     : TON;              (* pump fault timer *)
    SecondTimer     : TON;              (* run-time totalizer tick *)
  END_VAR

  VAR CONSTANT
    LT_RAW_MAX      : UDINT := 65535;   (* full-scale analog count; set to 4095 if
                                           level never reaches 100 % at full pot *)
    PUMP_MAX_RUN    : TIME  := T#60s;
  END_VAR

  (* ---------- Default setpoints on first scan (registers start at 0) ---------- *)
  IF SP_PumpOn = 0 AND SP_PumpOff = 0 THEN
    SP_PumpOn    := 70;
    SP_PumpOff   := 30;
    SP_HighAlarm := 90;
    SP_LowAlarm  := 10;
  END_IF;

  (* ---------- Scale level to percent ---------- *)
  LT_100_Pct := UDINT_TO_UINT((UINT_TO_UDINT(LT_100_Raw) * 100) / LT_RAW_MAX);
  IF LT_100_Pct > 100 THEN
    LT_100_Pct := 100;
  END_IF;

  (* ---------- Alarms ---------- *)
  LAH_100 := (LT_100_Pct >= SP_HighAlarm) OR LSHH_100;
  LAL_100 := LT_100_Pct <= SP_LowAlarm;

  (* Pump fault: ran longer than PUMP_MAX_RUN; latches until acknowledged *)
  MaxRunTimer(IN := P_101_Run, PT := PUMP_MAX_RUN);
  IF MaxRunTimer.Q THEN
    P_101_Fault := TRUE;
  END_IF;

  (* ---------- Start / stop requests ---------- *)
  StopRequest := HS_101B_Stop OR HMI_Stop;

  IF HMI_AutoMode THEN
    (* Auto: run between level setpoints *)
    StartRequest := LT_100_Pct >= SP_PumpOn;
    IF LT_100_Pct <= SP_PumpOff THEN
      RunLatch := FALSE;
    END_IF;
  ELSE
    (* Manual: local or SCADA start *)
    StartRequest := HS_101A_Start OR HMI_Start;
  END_IF;

  IF StartRequest THEN
    RunLatch := TRUE;
  END_IF;

  (* Stop priority and interlocks always win *)
  IF StopRequest OR LAL_100 OR P_101_Fault THEN
    RunLatch := FALSE;
  END_IF;

  P_101_Run := RunLatch;

  (* ---------- Inlet valve: operator command, overfill interlock ---------- *)
  XV_101_Open := HMI_ValveOpen AND NOT LAH_100;

  (* ---------- Horn: sounds on any new alarm, silenced by ACK ---------- *)
  AnyAlarm := LAH_100 OR LAL_100 OR P_101_Fault;
  AlarmEdge(CLK := AnyAlarm);
  IF AlarmEdge.Q THEN
    HornLatch := TRUE;
  END_IF;

  IF HMI_AlarmAck THEN
    HornLatch   := FALSE;
    P_101_Fault := FALSE;
  END_IF;

  XA_100_Horn := HornLatch;

  (* ---------- Counters ---------- *)
  StartEdge(CLK := P_101_Run);
  IF StartEdge.Q THEN
    P_101_Starts := P_101_Starts + 1;
  END_IF;

  SecondTimer(IN := P_101_Run AND NOT SecondTimer.Q, PT := T#1s);
  IF SecondTimer.Q THEN
    P_101_RunSec := P_101_RunSec + 1;
  END_IF;

  (* ---------- Heartbeat: SCADA raises comm-loss if this stops changing ---------- *)
  IF PLC_Heartbeat = 65535 THEN
    PLC_Heartbeat := 0;
  ELSE
    PLC_Heartbeat := PLC_Heartbeat + 1;
  END_IF;

  (* ---------- Clear momentary SCADA commands ---------- *)
  HMI_Start    := FALSE;
  HMI_Stop     := FALSE;
  HMI_AlarmAck := FALSE;

END_PROGRAM


CONFIGURATION Config0
  RESOURCE Res0 ON PLC
    TASK MainTask(INTERVAL := T#50ms, PRIORITY := 0);
    PROGRAM Inst0 WITH MainTask : TankPump;
  END_RESOURCE
END_CONFIGURATION
