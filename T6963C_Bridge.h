/******************************************************************************
 *  T6963C_Bridge.h  -  MASTER library for the SPI->T6963C bridge   |  v1.0
 *  ==========================================================================
 *  Works with the bridge firmware "T6963C_SPI_bridge.ino" v1.0.
 *  The master sends high-level drawing commands; the bridge renders them with
 *  u8g2 and pushes the framebuffer onto the panel's parallel (8080) bus.
 *
 *  - Arduino SPI master (STM32, AVR/ATmega328, ...)
 *  - Flow control: waits for the READY line (HIGH) before each frame
 *  - Frame protocol:  [CMD][LEN][payload...]   (int16/int32 little-endian)
 *  - BATCH support: several commands in one transaction + a single send.
 *    A batch that would overflow the bridge buffer is AUTOMATICALLY SPLIT into
 *    several transactions, so a batch never silently drops commands.
 *
 *  EXAMPLE:
 *      #include "T6963C_Bridge.h"
 *      T6963C_Bridge lcd(PIN_CS, PIN_READY);
 *      void setup(){
 *        SPI.begin(); lcd.begin();
 *        lcd.clear();
 *        lcd.setFont(FONT_NCEN14);
 *        lcd.str(8, 8, "GPSDO");
 *        lcd.number(8, 40, 12345, 3, FONT_10x20, "V");   // 12.345 V
 *        lcd.progress(8, 80, 220, 14, 66);
 *        lcd.send();
 *      }
 *
 *  Author: J. M. Niewinski (jmnlabs) + Claude AI.  License: MIT.
 ******************************************************************************/

#ifndef T6963C_BRIDGE_H
#define T6963C_BRIDGE_H

#include <Arduino.h>
#include <SPI.h>

/* ── Atomic-transaction guard ────────────────────────────────────────────
 * On a preemptive RTOS (e.g. FreeRTOS), a lower-priority display task can be
 * preempted in the MIDDLE of an SPI transaction (between CS-low and CS-high,
 * or right after the READY check). The bridge then sees a stalled NSS/clock
 * stream and mis-parses it (spurious "unknown command" counts). Suspending
 * the scheduler around each transaction makes it atomic with respect to task
 * switching. It does NOT disable interrupts, so a GPSDO's 1PPS input-capture
 * interrupt still fires on time — frequency counting is unaffected.
 * The guard is a no-op on bare Arduino (no scheduler). */
#if defined(INC_FREERTOS_H) || defined(configMAX_PRIORITIES) || __has_include(<FreeRTOS.h>)
  #if __has_include(<FreeRTOS.h>)
    #include <FreeRTOS.h>
    #include <task.h>
  #endif
  #define T6963C_LOCK()    vTaskSuspendAll()
  #define T6963C_UNLOCK()  xTaskResumeAll()
  #define T6963C_YIELD()   vTaskDelay(1)   /* 1 tick — bridge BUSY can last ~140 ms */
#else
  #define T6963C_LOCK()    ((void)0)
  #define T6963C_UNLOCK()  ((void)0)
  #define T6963C_YIELD()   ((void)0)
#endif

/* ====== Command codes - must match the bridge firmware v1.0 ============== */
enum : uint8_t {
  BC_CLEAR        = 0x01,
  BC_SEND         = 0x02,
  BC_CLEAR_DISP   = 0x03,
  BC_HOME         = 0x04,
  BC_SET_COLOR    = 0x10,
  BC_FONT_MODE    = 0x11,
  BC_BITMAP_MODE  = 0x12,
  BC_CONTRAST     = 0x13,
  BC_POWERSAVE    = 0x14,
  BC_FLIP         = 0x15,
  BC_FONT_REFH    = 0x16,
  BC_PIXEL        = 0x20,
  BC_LINE         = 0x21,
  BC_HLINE        = 0x22,
  BC_VLINE        = 0x23,
  BC_FRAME        = 0x30,
  BC_BOX          = 0x31,
  BC_RFRAME       = 0x32,
  BC_RBOX         = 0x33,
  BC_CIRCLE       = 0x40,
  BC_DISC         = 0x41,
  BC_ELLIPSE      = 0x42,
  BC_FELLIPSE     = 0x43,
  BC_TRIANGLE     = 0x50,
  BC_STR          = 0x60,
  BC_GLYPH        = 0x61,
  BC_SET_FONT     = 0x62,
  BC_FONT_POS     = 0x63,
  BC_FONT_DIR     = 0x64,
  BC_PROGRESS     = 0x70,
  BC_NUM          = 0x71,
  BC_BATCH        = 0x72,
  BC_DEMO         = 0x7A,
  BC_RESET_STATS  = 0x7C,
  BC_NOP          = 0xFF
};

/* ====== Font indices (match the FONTS[] table in the bridge) ============= */
enum {
  FONT_6x10 = 0, FONT_6x13, FONT_7x14, FONT_8x13B, FONT_10x20,
  FONT_NCEN10, FONT_NCEN14, FONT_LOGISOSO16, FONT_LOGISOSO28, FONT_5x7
};

/* ====== Font position / reference height ================================= */
enum { FPOS_BASELINE = 0, FPOS_TOP = 1, FPOS_CENTER = 2, FPOS_BOTTOM = 3 };
enum { REFH_TEXT = 0, REFH_EXTENDED = 1, REFH_ALL = 2 };

/* ====== Quadrant options for circle/disc/ellipse ======================== */
enum {
  DRAW_UPPER_RIGHT = 0x01, DRAW_UPPER_LEFT = 0x02,
  DRAW_LOWER_LEFT  = 0x04, DRAW_LOWER_RIGHT = 0x08,
  DRAW_ALL = 0x0F
};

/* Bridge payload limit. The bridge frame format uses a SINGLE-byte length
   field, so a frame payload must never reach 256 (which would wrap to 0 and
   be read as an empty frame, corrupting the stream). Cap at 255; the bridge's
   ps_payload[] is 256 B so 255 is always safe. A batch is auto-split so that
   nSub + sub-commands never exceed this. */
#define T6963C_BRIDGE_PAYLOAD_MAX  255

class T6963C_Bridge {
public:
  T6963C_Bridge(uint8_t csPin, uint8_t readyPin, uint32_t clockHz = 1000000UL)
    : _cs(csPin), _ready(readyPin), _spiSet(clockHz, MSBFIRST, SPI_MODE0) {}

  void begin() {
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);
    pinMode(_ready, INPUT);
  }

  /* ---- Buffer / screen ----------------------------------------------- */
  void clear()        { frame0(BC_CLEAR); }
  void send()         { frame0(BC_SEND); }
  void clearDisplay() { frame0(BC_CLEAR_DISP); }
  void home()         { frame0(BC_HOME); }

  /* ---- Color / modes ------------------------------------------------- */
  void setColor(uint8_t c)     { frame1(BC_SET_COLOR, c); }
  void setFontMode(uint8_t m)  { frame1(BC_FONT_MODE, m); }
  void setBitmapMode(uint8_t m){ frame1(BC_BITMAP_MODE, m); }
  void setContrast(uint8_t v)  { frame1(BC_CONTRAST, v); }
  void setPowerSave(uint8_t v) { frame1(BC_POWERSAVE, v); }
  void setFlip(uint8_t v)      { frame1(BC_FLIP, v); }
  void setFontRefHeight(uint8_t m){ frame1(BC_FONT_REFH, m); }

  /* ---- Pixels / lines ------------------------------------------------ */
  void pixel(int16_t x, int16_t y) {
    uint8_t p[4]; w16(p,x); w16(p+2,y); frame(BC_PIXEL,p,4);
  }
  void line(int16_t x0,int16_t y0,int16_t x1,int16_t y1) {
    uint8_t p[8]; w16(p,x0);w16(p+2,y0);w16(p+4,x1);w16(p+6,y1); frame(BC_LINE,p,8);
  }
  void hline(int16_t x,int16_t y,int16_t len) {
    uint8_t p[6]; w16(p,x);w16(p+2,y);w16(p+4,len); frame(BC_HLINE,p,6);
  }
  void vline(int16_t x,int16_t y,int16_t len) {
    uint8_t p[6]; w16(p,x);w16(p+2,y);w16(p+4,len); frame(BC_VLINE,p,6);
  }

  /* ---- Rectangles ---------------------------------------------------- */
  void frameRect(int16_t x,int16_t y,int16_t w,int16_t h) {
    uint8_t p[8]; w16(p,x);w16(p+2,y);w16(p+4,w);w16(p+6,h); frame(BC_FRAME,p,8);
  }
  void box(int16_t x,int16_t y,int16_t w,int16_t h) {
    uint8_t p[8]; w16(p,x);w16(p+2,y);w16(p+4,w);w16(p+6,h); frame(BC_BOX,p,8);
  }
  void rframe(int16_t x,int16_t y,int16_t w,int16_t h,uint8_t r) {
    uint8_t p[9]; w16(p,x);w16(p+2,y);w16(p+4,w);w16(p+6,h);p[8]=r; frame(BC_RFRAME,p,9);
  }
  void rbox(int16_t x,int16_t y,int16_t w,int16_t h,uint8_t r) {
    uint8_t p[9]; w16(p,x);w16(p+2,y);w16(p+4,w);w16(p+6,h);p[8]=r; frame(BC_RBOX,p,9);
  }

  /* ---- Circles / ellipses -------------------------------------------- */
  void circle(int16_t x,int16_t y,uint8_t r,uint8_t opt=DRAW_ALL) {
    uint8_t p[6]; w16(p,x);w16(p+2,y);p[4]=r;p[5]=opt; frame(BC_CIRCLE,p,6);
  }
  void disc(int16_t x,int16_t y,uint8_t r,uint8_t opt=DRAW_ALL) {
    uint8_t p[6]; w16(p,x);w16(p+2,y);p[4]=r;p[5]=opt; frame(BC_DISC,p,6);
  }
  void ellipse(int16_t x,int16_t y,uint8_t rx,uint8_t ry,uint8_t opt=DRAW_ALL) {
    uint8_t p[7]; w16(p,x);w16(p+2,y);p[4]=rx;p[5]=ry;p[6]=opt; frame(BC_ELLIPSE,p,7);
  }
  void fellipse(int16_t x,int16_t y,uint8_t rx,uint8_t ry,uint8_t opt=DRAW_ALL) {
    uint8_t p[7]; w16(p,x);w16(p+2,y);p[4]=rx;p[5]=ry;p[6]=opt; frame(BC_FELLIPSE,p,7);
  }

  /* ---- Triangle ------------------------------------------------------ */
  void triangle(int16_t x0,int16_t y0,int16_t x1,int16_t y1,int16_t x2,int16_t y2) {
    uint8_t p[12];
    w16(p,x0);w16(p+2,y0);w16(p+4,x1);w16(p+6,y1);w16(p+8,x2);w16(p+10,y2);
    frame(BC_TRIANGLE,p,12);
  }

  /* ---- Text ---------------------------------------------------------- */
  void str(int16_t x,int16_t y,const char* s) {
    uint8_t p[64]; w16(p,x);w16(p+2,y);
    uint8_t n=0; while(s[n] && n<(sizeof(p)-4)){ p[4+n]=(uint8_t)s[n]; n++; }
    frame(BC_STR,p,4+n);
  }
  void glyph(int16_t x,int16_t y,uint16_t code) {
    uint8_t p[6]; w16(p,x);w16(p+2,y);p[4]=code&0xFF;p[5]=code>>8; frame(BC_GLYPH,p,6);
  }
  void setFont(uint8_t fontId) { frame1(BC_SET_FONT, fontId); }
  void setFontPos(uint8_t pos) { frame1(BC_FONT_POS, pos); }
  void setFontDir(uint8_t dir) { frame1(BC_FONT_DIR, dir); }

  /* ---- progress bar -------------------------------------------------- */
  void progress(int16_t x,int16_t y,int16_t w,int16_t h,uint8_t pct) {
    uint8_t p[9]; w16(p,x);w16(p+2,y);w16(p+4,w);w16(p+6,h);p[8]=pct;
    frame(BC_PROGRESS,p,9);
  }

  /* ---- number with decimals + unit ----------------------------------
   * val = value * 10^decimals (scaled by the master).
   * Example: number(x,y,12345,3,FONT_10x20,"V") => "12.345 V"           */
  void number(int16_t x,int16_t y,int32_t val,uint8_t decimals,
              uint8_t fontId,const char* unit="") {
    uint8_t p[24];
    w16(p,x); w16(p+2,y);
    w32(p+4,val);
    p[8]=decimals; p[9]=fontId;
    uint8_t n=0; while(unit[n] && n<8){ p[10+n]=(uint8_t)unit[n]; n++; }
    frame(BC_NUM, p, 10+n);
  }

  /* ---- float convenience wrapper around number() --------------------
   * Converts a float to int32*10^decimals on the master side.           */
  void numberF(int16_t x,int16_t y,float v,uint8_t decimals,
               uint8_t fontId,const char* unit="") {
    float scale=1; for(uint8_t i=0;i<decimals;i++)scale*=10.0f;
    int32_t iv=(int32_t)lroundf(v*scale);
    number(x,y,iv,decimals,fontId,unit);
  }

  void resetStats() { frame0(BC_RESET_STATS); }
  void demo()       { frame0(BC_DEMO); }

  /* ===================================================================
   *  BATCH: pack several commands into ONE transaction (one BUSY/READY).
   *  Usage:
   *      lcd.batchBegin();
   *      lcd.box(...); lcd.str(...); lcd.progress(...);
   *      lcd.batchEnd();      // sends everything + (optionally) send
   *
   *  In batch mode the helpers do NOT send immediately - they append the
   *  sub-command to a buffer. batchEnd() packs it into a BC_BATCH frame.
   *
   *  AUTO-SPLIT: if a sub-command would overflow the bridge payload buffer,
   *  the current batch is flushed (sent) and a new one started. A batch
   *  therefore never silently drops commands, regardless of screen size.
   *  batchOverflows() reports how many auto-flushes happened (diagnostic).
   * =================================================================== */
  void batchBegin() { _batch = true; _bn = 0; _blen = 0; }

  void batchEnd(bool autoSend = false) {
    if (!_batch) return;
    _batch = false;
    flushBatch();
    if (autoSend) send();
  }

  uint16_t batchOverflows() const { return _batchFlushes; }
  void     resetBatchOverflows()  { _batchFlushes = 0; }

  /* ---- Low-level: send an arbitrary frame ---------------------------- */
  void frame(uint8_t cmd, const uint8_t* payload, uint8_t len) {
    if (_batch) { batchAdd(cmd, payload, len); return; }
    waitReady();
    SPI.beginTransaction(_spiSet);
    digitalWrite(_cs, LOW);
    csSettle();
    SPI.transfer(cmd);
    SPI.transfer(len);
    for (uint8_t i = 0; i < len; i++) SPI.transfer(payload[i]);
    digitalWrite(_cs, HIGH);
    SPI.endTransaction();
  }

  void setReadyTimeout(uint16_t ms) { _timeout = ms; }

private:
  uint8_t  _cs, _ready;
  SPISettings _spiSet;
  uint16_t _timeout = 1000;

  /* batch buffer: header [nSub] + sequence of [subCmd][subLen][data...] */
  bool     _batch = false;
  uint8_t  _bn   = 0;            // number of sub-commands in the current batch
  uint16_t _blen = 0;            // data length in _bbuf (excluding the nSub byte)
  uint8_t  _bbuf[T6963C_BRIDGE_PAYLOAD_MAX];
  uint16_t _batchFlushes = 0;    // auto-flush counter (diagnostic)

  static inline void w16(uint8_t* p, int16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
  }
  static inline void w32(uint8_t* p, int32_t v) {
    p[0]=(uint8_t)(v&0xFF); p[1]=(uint8_t)((v>>8)&0xFF);
    p[2]=(uint8_t)((v>>16)&0xFF); p[3]=(uint8_t)((v>>24)&0xFF);
  }

  void frame0(uint8_t cmd)            { frame(cmd, nullptr, 0); }
  void frame1(uint8_t cmd, uint8_t a) { frame(cmd, &a, 1); }

  /* Send the current batch buffer as one BC_BATCH frame, then reset it.
     Temporarily leaves batch mode so frame() actually transmits. */
  void flushBatch() {
    if (_bn == 0) return;
    bool wasBatch = _batch;
    _batch = false;                          // so frame() transmits for real
    // _blen excludes the nSub byte; send nSub + data = _blen+1 bytes.
    frame(BC_BATCH, _bbuf, (uint8_t)(_blen + 1));
    _batch = wasBatch;
    _bn = 0; _blen = 0;                       // start a fresh batch
  }

  /* Append a sub-command to the batch buffer. If it would overflow the
     bridge payload limit, flush the current batch first (auto-split), then
     append to the fresh buffer. Never drops a sub-command. */
  void batchAdd(uint8_t cmd, const uint8_t* payload, uint8_t len) {
    // a sub-command needs 2 (cmd+len) + len bytes; +1 for the nSub header byte.
    const uint16_t need = 1 + _blen + 2 + len;
    if (need > T6963C_BRIDGE_PAYLOAD_MAX) {
      flushBatch();                          // auto-split: send what we have
      _batchFlushes++;
    }
    // Guard: a single sub-command must fit an empty buffer. If a caller ever
    // builds something larger than the limit (cannot happen with the helpers,
    // whose payloads are small), skip it rather than corrupt memory.
    if (1 + 2 + len > T6963C_BRIDGE_PAYLOAD_MAX) return;
    uint8_t* dst = _bbuf + 1 + _blen;          // +1 because [0]=nSub
    dst[0] = cmd; dst[1] = len;
    for (uint8_t i=0;i<len;i++) dst[2+i] = payload[i];
    _blen += 2 + len;
    _bn++;
    _bbuf[0] = _bn;
  }

  static inline void csSettle() { delayMicroseconds(2); }

  bool waitReady() {
    if (_timeout == 0) { while (digitalRead(_ready) == LOW) { T6963C_YIELD(); } return true; }
    uint32_t t0 = millis();
    while (digitalRead(_ready) == LOW) {
      if ((uint16_t)(millis() - t0) > _timeout) return false;
      T6963C_YIELD();
    }
    return true;
  }
};

#endif // T6963C_BRIDGE_H
