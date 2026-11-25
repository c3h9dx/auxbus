/* =====================================================================
 * auxbus_tool — Arduino Uno as an Elecraft AUXBUS monitor and transmitter.
 * Protocol description: AUXBUS.md.
 *
 * - receives K* (radio, 476.1 us/bit) and ACC (accessory, 1000 us/bit)
 *   frames and prints them together with their interpretation,
 * - AXccdd;  sends a K* CMD frame (cc = command, dd = data) and immediately
 *            prints the response, read the same way the K4 reads it,
 * - AAdd;    sends an ACC frame with byte dd (00..7F),
 * - PU1; / PU0;  enables / disables the AUXBUS pull-up to 1 (D2 pull-up).
 *
 * Hardware: AUXBUS on D2 (INT0), ground on GND. Open-drain output:
 *   '0' = output LOW, '1' / idle = input (Hi-Z, optional pull-up).
 *
 * Output (115200 bit/s): direction, source, type, fields, interpretation,
 * warnings after "!":
 *   RX K* BAND cmd=0x20 data=0x02 response=0xFF  band 40m
 *   RX K* FREQ value=0x437828  7105 kHz, checksum OK
 *   RX ACC STATE byte=0x37  on (KPA1500, RADIO TYPE = K4)
 *   TX K* AC cmd=0x35 data=0x01 response=0x80  KRC2 AC outputs
 *
 * Sampling (as done by the frame's receiver, from the rising edge t_r that
 * ends the initial LOW):
 *   K*  : t_r + 714.15 + 476.1*k us, k = 0..23 (in CMD, samples 16..23 = response)
 *   ACC : t_r + 5500.2 + 1000.1*k us, k = 0..6
 *   Frame type from the LOW length: < 10 ms K*, >= 10 ms ACC.
 *   A LOW that starts after the last response sample but before
 *   t_r + 17 bits + 4.4 ms is received by the K4 as an ACC frame (response
 *   tail); this program does the same.
 * ===================================================================== */

#include <Arduino.h>

// Frame warnings for printing (defined before the first function because of Arduino prototypes)
struct Warn { bool coll; bool tail; uint32_t bad_pre_tk; int8_t near; };

// Command line being typed (echoed in real time)
static char    g_line[16];
static uint8_t g_len = 0;

// ---- AUXBUS on D2 = PD2 ----
#define AUX_BIT        _BV(PD2)
static bool g_pullup = false;
#define AUX_LOW()      do { PORTD &= ~AUX_BIT; DDRD |= AUX_BIT; } while (0)
#define AUX_RELEASE()  do { DDRD &= ~AUX_BIT; if (g_pullup) PORTD |= AUX_BIT; else PORTD &= ~AUX_BIT; } while (0)
#define AUX_READ()     ((PIND & AUX_BIT) != 0)

// ---- Receive timing in Timer1 ticks (0.5 us) ----
#define K_SAMPLE_TK(k)   ((14283UL + 9522UL * (k) + 5) / 10)    // t_r + 714.15 + 476.1*k us
#define K_END_TK         23805UL      // t_r + 25 bits = end of the last FREQ bit
#define K_HALF_TK        476UL        // half a K* bit
#define K_TAIL_TK        24987UL      // t_r + 17 bits + 4.4 ms: end of the response tail search
#define K_MARGIN_TK      95UL         // edge closer than 10 % of a bit to a sample = warning
#define ACC_SAMPLE_TK(k) ((110004UL + 20002UL * (k) + 5) / 10)  // t_r + 5500.2 + 1000.1*k us
#define ACC_END_TK       24202UL      // t_r + 12.1 ms
#define ACC_MARGIN_TK    200UL        // 10 % of an ACC bit
#define ACC_LOW_TK       20000UL      // LOW >= 10 ms = ACC frame
#define PRE_MIN_TK       3200UL       // K* preamble LOW: 1.6 ..
#define PRE_MAX_TK       4400UL       //                  .. 2.2 ms
#define GLITCH_TK        100UL        // LOW < 50 us = glitch

// ---- Transmit timing ----
static const uint32_t K_PRE_LOW_X10 = 18961;   // 1896.1 us (in 0.1 us units)
static const uint32_t K_BIT_X10     = 4761;    // 476.1 us
static const uint32_t K_HALF_X10    = 2381;    // 238.1 us
static const uint32_t ACC_LOW_US    = 79910;   // LOW before an ACC frame
static const uint16_t ACC_SYNC_US   = 5000;    // 5 x '1'
static const uint16_t ACC_BIT_US    = 1000;
static const uint16_t IDLE_REQ_MS   = 15;      // required idle bus before transmitting
static const uint16_t IDLE_TMO_MS   = 2000;

static uint8_t g_ax_last = 0xFF;               // last response (AX;)

// =====================================================================
// Timer1: free-running, prescaler 8 -> 0.5 us/tick
// =====================================================================
static volatile uint16_t g_t1_ovf = 0;
ISR(TIMER1_OVF_vect) { g_t1_ovf++; }

static void t1_init(void) {
  TCCR1A = 0; TCCR1B = _BV(CS11); TCNT1 = 0;
  TIFR1 = _BV(TOV1); TIMSK1 = _BV(TOIE1);
}
static inline uint32_t t1_now_isr(void) {              // call with interrupts disabled
  uint16_t lo = TCNT1, hi = g_t1_ovf;
  if ((TIFR1 & _BV(TOV1)) && lo < 0x8000) hi++;
  return ((uint32_t)hi << 16) | lo;
}
static inline uint32_t t1_now(void) { uint8_t s = SREG; cli(); uint32_t t = t1_now_isr(); SREG = s; return t; }
static inline void t1_wait_from(uint16_t t0, uint16_t us) {   // until t0 + us (max 32 ms)
  const uint16_t tk = us * 2; while ((uint16_t)(TCNT1 - t0) < tk) {}
}
static inline uint16_t x10_us(uint32_t x10) { return (uint16_t)((x10 + 5) / 10); }

// =====================================================================
// Printing
// =====================================================================
static const char *const FAULT_TXT[16] = {
  "powered on", "power-off problem", "current 1", "current 2",
  "temperature", "firmware rev. changed", "power input too high", "frequency counter",
  "60 volt power supply", "SWR too high", "T/R switch failure", "PA failure",
  "PA output too high", "60 V too low", "MON270 signal", "excess gain"
};
static const uint8_t K_BAND_M[11]   = {160, 80, 40, 30, 20, 17, 15, 12, 10, 60, 6};   // BAND data -> metres
static const uint8_t ACC_BAND_M[11] = {60, 160, 80, 40, 30, 20, 17, 15, 12, 10, 6};   // ACC byte -> metres

static void print_hex(uint32_t v, uint8_t digits) {
  for (int8_t i = digits - 1; i >= 0; i--) Serial.print((v >> (4 * i)) & 0xF, HEX);
}
static void print_0x(uint32_t v, uint8_t digits) { Serial.print(F("0x")); print_hex(v, digits); }

static inline bool k_is_cmd(uint8_t b) { return !(b >= 0x40 && b <= 0x7F); }

static bool freq_ok(uint32_t v) {
  uint8_t s = 0;
  for (uint8_t i = 0; i < 24; i += 4) s += (v >> i) & 0xF;
  return (v & 0xC00010UL) == 0x400000UL && (s & 0xF) == 0;
}

// A received frame printed while a command is being typed: move the typed
// text out of the way, print the frame, then show the typed text again.
static void input_hide(void) { if (g_len) Serial.println(); }
static void input_show(void) { if (g_len) Serial.write((const uint8_t *)g_line, g_len); }

static void print_warn(const Warn &w) {
  if (w.tail) Serial.print(F("  ! response tail, K4 reads it as an ACC frame"));
  if (w.coll) Serial.print(F("  ! collision"));
  if (w.bad_pre_tk) { Serial.print(F("  ! preamble LOW ")); Serial.print(w.bad_pre_tk / 2); Serial.print(F(" us")); }
  if (w.near >= 0) { Serial.print(F("  ! edge near sample ")); Serial.print(w.near); }
  Serial.println();
  input_show();
}

// K* frame as 24 bits: cmd, data, response, or FREQ.
static void print_k(const __FlashStringHelper *dir, uint32_t v, const Warn &w) {
  uint8_t cmd = v >> 16, data = (v >> 8) & 0xFF, resp = v & 0xFF;
  input_hide();
  Serial.print(dir); Serial.print(F(" K* "));
  if (!k_is_cmd(cmd)) {
    Serial.print(F("FREQ value=")); print_0x(v, 6); Serial.print(F("  "));
    Serial.print((v & 0x3FFFE0UL) >> 5); Serial.print(F(" kHz, checksum "));
    Serial.print(freq_ok(v) ? F("OK") : F("BAD"));
  } else {
    Serial.print(cmd == 0x20 ? F("BAND") : cmd == 0x21 ? F("XVTR") : cmd == 0x34 ? F("STATUS") :
                 cmd == 0x35 ? F("AC") : cmd == 0x38 ? F("ANT") : cmd == 0x39 ? F("ATU") : F("CMD"));
    Serial.print(F(" cmd=")); print_0x(cmd, 2);
    Serial.print(F(" data=")); print_0x(data, 2);
    Serial.print(F(" response=")); print_0x(resp, 2);
    Serial.print(F("  "));
    switch (cmd) {
      case 0x20:
        Serial.print(F("band "));
        if (data < 11) { Serial.print(K_BAND_M[data]); Serial.print('m'); }
        else { Serial.print(F("? (")); Serial.print(data); Serial.print(')'); }
        break;
      case 0x21:
        if (data == 0) Serial.print(F("XVTR off (HF band)"));
        else { Serial.print(F("XVTR slot ")); Serial.print(data); }
        break;
      case 0x34: Serial.print(F("KRC2 status char")); break;
      case 0x35: Serial.print(F("KRC2 AC outputs")); break;
      case 0x38:
        Serial.print(data == 0 ? F("next antenna") : data == 1 ? F("antenna 1") :
                     data == 2 ? F("antenna 2") : F("antenna ? (ignored by KPA1500)"));
        break;
      case 0x39:
        Serial.print(data == 0 ? F("ATU tune") : F("ATU ? (ignored by KPA1500)"));
        break;
      default: Serial.print(F("unknown command"));
    }
  }
  print_warn(w);
}

static void print_acc(const __FlashStringHelper *dir, uint8_t b, const Warn &w) {
  uint8_t x = b & 0x3F;
  input_hide();
  Serial.print(dir); Serial.print(F(" ACC "));
  if (x <= 0x0A)                    Serial.print(F("BAND"));
  else if (x >= 0x20 && x <= 0x2F)  Serial.print(F("FAULT"));
  else if (x == 0x31 || x == 0x32 || x == 0x33 || x == 0x34 || x == 0x37) Serial.print(F("STATE"));
  else if (x == 0x35 || x == 0x36)  Serial.print(F("KEY"));
  else                              Serial.print('?');
  Serial.print(F(" byte=")); print_0x(b, 2); Serial.print(F("  "));
  if (x <= 0x0A) { Serial.print(F("band button ")); Serial.print(ACC_BAND_M[x]); Serial.print('m'); }
  else if (x >= 0x20 && x <= 0x2F) {
    Serial.print(F("fault ")); Serial.print(x & 15); Serial.print(F(": ")); Serial.print(FAULT_TXT[x & 15]);
  } else switch (x) {
    case 0x31: Serial.print(F("on")); break;
    case 0x32: Serial.print(F("off")); break;
    case 0x33: Serial.print(F("standby")); break;
    case 0x34: Serial.print(F("operate")); break;
    case 0x35: Serial.print(F("key start, K4 enters TUNE")); break;
    case 0x36: Serial.print(F("key stop, K4 leaves TUNE")); break;
    case 0x37: Serial.print(F("on (KPA1500, RADIO TYPE = K4)")); break;
    default:   Serial.print(F("unknown, ignored by K4"));
  }
  if (b & 0x40) Serial.print(F(", OPER"));
  print_warn(w);
}

// =====================================================================
// RECEIVE: INT0 stores every edge with a timestamp, loop() decodes
// =====================================================================
#define RX_RING   64          // power of 2
#define RX_EDGES  48

struct Edge { uint32_t t; uint8_t lvl; };
static volatile Edge    g_ring[RX_RING];
static volatile uint8_t g_ring_wr = 0, g_ring_ovf = 0;
static uint8_t          g_ring_rd = 0;

ISR(INT0_vect) {
  uint8_t lvl = AUX_READ();
  uint8_t n = (g_ring_wr + 1) & (RX_RING - 1);
  if (n == g_ring_rd) { g_ring_ovf = 1; return; }
  g_ring[g_ring_wr].t = t1_now_isr();
  g_ring[g_ring_wr].lvl = lvl;
  g_ring_wr = n;
}
static inline void rx_enable(bool on) {
  if (on) { EIFR = _BV(INTF0); EIMSK |= _BV(INT0); } else EIMSK &= ~_BV(INT0);
}

enum { ST_IDLE, ST_LOW, ST_K, ST_ACC };
static uint8_t  g_st = ST_IDLE;
static uint8_t  g_lvl = 1;                    // line level after the last edge
static uint32_t g_t_f, g_t_r;                 // start of LOW, rising edge
static uint32_t g_low_tk;                     // LOW length
static bool     g_f_tail, g_f_coll, g_low_warned;
static bool     g_tail_armed = false;         // after a CMD frame: look for a response tail
static uint32_t g_tail_until;
static uint32_t g_ev_t[RX_EDGES];             // edges after t_r
static uint8_t  g_ev_l[RX_EDGES], g_ev_n;

static void start_low(uint32_t t, bool tail, bool coll) {
  g_st = ST_LOW; g_t_f = t; g_f_tail = tail; g_f_coll = coll; g_low_warned = false;
  g_tail_armed = false;
}

// line level at time ts (relative to t_r) + warning about a nearby edge
static uint8_t sample_at(uint32_t ts, int8_t k, uint32_t margin, int8_t *near) {
  uint8_t l = 1;                              // the line is HIGH from t_r
  for (uint8_t i = 0; i < g_ev_n; i++) {
    uint32_t te = g_ev_t[i] - g_t_r;
    if (te <= ts) l = g_ev_l[i];
    uint32_t d = te > ts ? te - ts : ts - te;
    if (d < margin && near) *near = k;
  }
  return l;
}

static uint32_t k_done_tk(void) {             // when the K* frame is complete
  uint8_t b = 0;
  for (uint8_t k = 0; k < 8; k++) b = (b << 1) | sample_at(K_SAMPLE_TK(k), k, 0, NULL);
  return k_is_cmd(b) ? K_SAMPLE_TK(23) + 1 : K_END_TK + K_HALF_TK;
}

static void finish_k(void) {
  Warn w = { g_f_coll, false, 0, -1 };
  if (g_low_tk < PRE_MIN_TK || g_low_tk > PRE_MAX_TK) w.bad_pre_tk = g_low_tk;
  uint32_t v = 0;
  for (uint8_t k = 0; k < 24; k++) v = (v << 1) | sample_at(K_SAMPLE_TK(k), k, K_MARGIN_TK, &w.near);
  bool cmd = k_is_cmd(v >> 16);
  uint32_t t_done = g_t_r + (cmd ? K_SAMPLE_TK(23) + 1 : K_END_TK + K_HALF_TK);
  print_k(F("RX"), v, w);
  g_st = ST_IDLE;
  if (g_lvl == 0) start_low(t_done, cmd, !cmd);          // line still LOW: tail or collision
  else { g_tail_armed = cmd; g_tail_until = g_t_r + K_TAIL_TK; }
}

static void finish_acc(void) {
  Warn w = { g_f_coll, g_f_tail, 0, -1 };
  uint8_t b = 0;
  for (uint8_t k = 0; k < 7; k++) b = (b << 1) | sample_at(ACC_SAMPLE_TK(k), k, ACC_MARGIN_TK, &w.near);
  print_acc(F("RX"), b, w);
  g_st = ST_IDLE; g_tail_armed = false;
  if (g_lvl == 0) start_low(g_t_r + ACC_END_TK, false, true);
}

static void check_time(uint32_t now) {
  if (g_st == ST_K && now - g_t_r >= k_done_tk()) finish_k();
  else if (g_st == ST_ACC && now - g_t_r >= ACC_END_TK) finish_acc();
  if (g_st == ST_LOW && !g_low_warned && now - g_t_f > 2000000UL) {   // 1 s
    input_hide(); Serial.println(F("RX ! AUXBUS held LOW for more than 1 s")); input_show();
    g_low_warned = true;
  }
}

static void on_edge(uint32_t t, uint8_t l) {
  if (l == g_lvl) return;                      // duplicate
  check_time(t);
  switch (g_st) {
    case ST_IDLE:
      if (l == 0) start_low(t, g_tail_armed && (int32_t)(t - g_tail_until) < 0, false);
      break;
    case ST_LOW:
      if (l == 1) {
        g_low_tk = t - g_t_f;
        if (g_low_tk < GLITCH_TK && !g_f_tail) { g_st = ST_IDLE; break; }
        g_t_r = t; g_ev_n = 0;
        g_st = (g_f_tail || g_low_tk >= ACC_LOW_TK) ? ST_ACC : ST_K;
      }
      break;
    case ST_K:
    case ST_ACC:
      if (g_ev_n < RX_EDGES) { g_ev_t[g_ev_n] = t; g_ev_l[g_ev_n] = l; g_ev_n++; }
      break;
  }
  g_lvl = l;
}

static void rx_poll(void) {
  if (g_ring_ovf) { g_ring_ovf = 0; g_st = ST_IDLE; input_hide(); Serial.println(F("RX ! edge buffer overflow")); input_show(); }
  while (g_ring_rd != g_ring_wr) {
    uint32_t t = g_ring[g_ring_rd].t; uint8_t l = g_ring[g_ring_rd].lvl;
    g_ring_rd = (g_ring_rd + 1) & (RX_RING - 1);
    on_edge(t, l);
  }
  check_time(t1_now());
}

// Resume receiving after our own transmission (call with interrupts disabled).
// tail = after a CMD frame: a LOW before t_r + K_TAIL_TK is a response tail.
static void rx_resume(bool tail, uint32_t t_r) {
  g_ring_rd = g_ring_wr;
  rx_enable(true);
  uint32_t now = t1_now_isr();
  g_lvl = AUX_READ();
  g_st = ST_IDLE;
  g_tail_armed = tail; g_tail_until = t_r + K_TAIL_TK;
  if (!g_lvl) start_low(now, tail && (int32_t)(now - g_tail_until) < 0, false);
}

// =====================================================================
// TRANSMIT
// =====================================================================
static bool wait_bus_idle(void) {              // IDLE_REQ_MS of continuous HIGH
  AUX_RELEASE();
  uint32_t t_start = millis(), t_high = millis();
  while (millis() - t_start < IDLE_TMO_MS) {
    rx_poll();
    if (!AUX_READ() || g_st != ST_IDLE) t_high = millis();
    else if (millis() - t_high >= IDLE_REQ_MS) return true;
  }
  Serial.println(F("! AUXBUS busy (no 15 ms idle), not sent"));
  return false;
}

// AXccdd: K* CMD frame with K4 timing, then 8 response samples like the K4.
static void tx_k(uint16_t frame) {
  if (!wait_bus_idle()) return;
  rx_enable(false);
  uint8_t sreg = SREG;
  noInterrupts();                              // ~13.6 ms in total
  const uint32_t t0_32 = t1_now_isr();
  const uint16_t t0 = (uint16_t)t0_32;
  AUX_LOW();                                   // preamble: 4 x 0 ...
  t1_wait_from(t0, x10_us(K_PRE_LOW_X10));
  AUX_RELEASE();                               // ... and 1 x 1
  for (uint8_t m = 0; m < 16; m++) {
    t1_wait_from(t0, x10_us(K_PRE_LOW_X10 + K_BIT_X10 * (m + 1)));
    if ((frame >> (15 - m)) & 1) AUX_RELEASE(); else AUX_LOW();
  }
  const uint32_t end_x10 = K_PRE_LOW_X10 + K_BIT_X10 * 17;
  t1_wait_from(t0, x10_us(end_x10));
  AUX_RELEASE();                               // end of frame, line released
  uint8_t r = 0;
  for (uint8_t k = 0; k < 8; k++) {            // t_end + 238.1 + 476.1*k us
    t1_wait_from(t0, x10_us(end_x10 + K_HALF_X10 + K_BIT_X10 * k));
    r = (r << 1) | (AUX_READ() ? 1 : 0);
  }
  rx_resume(true, t0_32 + (2 * K_PRE_LOW_X10 + 5) / 10);
  SREG = sreg;
  g_ax_last = r;
  Warn w = { false, false, 0, -1 };
  print_k(F("TX"), ((uint32_t)frame << 8) | r, w);
}

// AAdd: ACC frame
static void tx_acc(uint8_t b) {
  if (!wait_bus_idle()) return;
  Warn w = { false, false, 0, -1 };
  print_acc(F("TX"), b, w);
  Serial.flush();
  rx_enable(false);
  AUX_LOW();                                   // LOW ~80 ms (interrupts enabled)
  delay(ACC_LOW_US / 1000);
  delayMicroseconds(ACC_LOW_US % 1000);
  uint8_t sreg = SREG;
  noInterrupts();
  AUX_RELEASE();                               // t_r
  const uint16_t t0 = TCNT1;
  uint16_t t = ACC_SYNC_US;
  for (int8_t i = 6; i >= 0; i--) {
    t1_wait_from(t0, t);
    if ((b >> i) & 1) AUX_RELEASE(); else AUX_LOW();
    t += ACC_BIT_US;
  }
  t1_wait_from(t0, t);                         // end of b0 = t_r + 12 ms
  AUX_RELEASE();
  const uint16_t tw = TCNT1;                   // the line returns to HIGH through the bus RC:
  while (!AUX_READ() && (uint16_t)(TCNT1 - tw) < 2 * 2000U) {}   // wait max 2 ms so our own
  rx_resume(false, 0);                         // slow edge is not taken as a new frame
  SREG = sreg;
}

// =====================================================================
// Commands (terminated by ';', Enter, or both)
// =====================================================================
static void help(void) {
  Serial.println(F("auxbus_tool: AXccdd; send K* CMD frame + read response"));
  Serial.println(F("             AX; last response"));
  Serial.println(F("             AAdd; send ACC frame (00-7F)"));
  Serial.println(F("             PU1; / PU0; AUXBUS pull-up on/off"));
  Serial.println(F("             PU;"));
  Serial.println(F("             ?;"));
  Serial.print(F("state: pull-up=")); Serial.print(g_pullup ? F("on") : F("off"));
  Serial.print(F(" line=")); Serial.println(AUX_READ() ? F("HIGH") : F("LOW"));
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
static int32_t parse_hex(const char *p, uint8_t n) {      // exactly n hex digits
  int32_t v = 0;
  for (uint8_t i = 0; i < n; i++) { int h = hexval(p[i]); if (h < 0) return -1; v = (v << 4) | h; }
  return p[n] == 0 ? v : -1;
}

static void handle(char *s) {
  for (char *p = s; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
  if (s[0] == '?' && !s[1]) { help(); return; }
  if (s[0] == 'A' && s[1] == 'X') {
    if (!s[2]) { Serial.print(F("AX")); print_hex(g_ax_last, 2); Serial.println(';'); return; }
    int32_t v = parse_hex(s + 2, 4);
    if (v >= 0) { tx_k((uint16_t)v); return; }
  } else if (s[0] == 'A' && s[1] == 'A') {
    int32_t v = parse_hex(s + 2, 2);
    if (v >= 0 && v <= 0x7F) { tx_acc((uint8_t)v); return; }
  } else if (s[0] == 'P' && s[1] == 'U') {
    if (s[2] == '0' || s[2] == '1') {
      if (s[3]) { Serial.println(F("?;")); return; }
      g_pullup = (s[2] == '1');
      uint8_t sreg = SREG; noInterrupts();
      if (!(DDRD & AUX_BIT)) AUX_RELEASE();   // do not touch the line while transmitting
      SREG = sreg;
    } else if (s[2]) { Serial.println(F("?;")); return; }
    Serial.print(F("PU")); Serial.print(g_pullup ? '1' : '0'); Serial.println(';');
    return;
  }
  Serial.println(F("?;"));
}

void setup() {
  AUX_RELEASE();
  t1_init();
  EICRA = (EICRA & ~(_BV(ISC01) | _BV(ISC00))) | _BV(ISC00);   // INT0 on every edge
  noInterrupts();
  rx_resume(false, 0);
  interrupts();
  Serial.begin(115200);
  Serial.println(F("auxbus_tool ready (AUXBUS on D2, open-drain)"));
  help();
}

void loop() {
  rx_poll();
  while (Serial.available()) {
    char c = Serial.read();
    if (c == ';' || ((c == '\n' || c == '\r') && g_len)) {    // end of command
      if (c == ';') Serial.print(';');
      Serial.println();
      g_line[g_len] = 0;
      char cmd[sizeof(g_line)];
      memcpy(cmd, g_line, g_len + 1);
      g_len = 0;                                     // nothing typed while handling
      if (cmd[0]) handle(cmd);
    } else if (c == 0x08 || c == 0x7F) {             // backspace / delete
      if (g_len) { g_len--; Serial.print(F("\b \b")); }
    } else if (c > ' ' && c < 0x7F && g_len < sizeof(g_line) - 1) {
      g_line[g_len++] = c;
      Serial.print(c);                               // real-time echo
    }
  }
}
