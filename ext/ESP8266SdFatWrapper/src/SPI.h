#pragma once

#include "Arduino.h"

#include <inttypes.h>

#include "hardware/spi.h"

#define DEBUGSPI(...) do { } while(0)

typedef enum {
  SPI_MODE0 = 0,
  SPI_MODE1 = 1,
  SPI_MODE2 = 2,
  SPI_MODE3 = 3,
} SPIMode;

class SPISettings {
  public:
  SPISettings(uint32_t clock, BitOrder bitOrder, SPIMode dataMode) {
    if (__builtin_constant_p(clock)) {
      init_AlwaysInline(clock, bitOrder, dataMode);
    } else {
      init_MightInline(clock, bitOrder, dataMode);
    }
  }

  SPISettings(uint32_t clock, BitOrder bitOrder, int dataMode) {
    if (__builtin_constant_p(clock)) {
      init_AlwaysInline(clock, bitOrder, (SPIMode)dataMode);
    } else {
      init_MightInline(clock, bitOrder, (SPIMode)dataMode);
    }
  }

  // Default speed set to 4MHz, SPI mode set to MODE 0 and Bit order set to MSB first.
  SPISettings() { init_AlwaysInline(4000000, MSBFIRST, SPI_MODE0); }

  bool operator==(const SPISettings& rhs) const
  {
    if ((this->clockFreq == rhs.clockFreq) &&
        (this->bitOrder == rhs.bitOrder) &&
        (this->dataMode == rhs.dataMode)) {
      return true;
    }
    return false;
  }

  bool operator!=(const SPISettings& rhs) const
  {
    return !(*this == rhs);
  }

  uint32_t getClockFreq() const {
    return clockFreq;
  }
  SPIMode getDataMode() const {
    return dataMode;
  }
  BitOrder getBitOrder() const {
    return (bitOrder);
  }

  private:
  void init_MightInline(uint32_t clock, BitOrder bitOrder, SPIMode dataMode) {
    init_AlwaysInline(clock, bitOrder, dataMode);
  }

  // Core developer MUST use an helper function in beginTransaction() to use this data
  void init_AlwaysInline(uint32_t clock, BitOrder bitOrder, SPIMode dataMode) __attribute__((__always_inline__)) {
    this->clockFreq = clock;
    this->dataMode = dataMode;
    this->bitOrder = bitOrder;
  }

  uint32_t clockFreq;
  SPIMode dataMode;
  BitOrder bitOrder;
};

class HardwareSPI
{
  public:
    virtual ~HardwareSPI() { }

    virtual uint8_t transfer(uint8_t data) = 0;
    virtual uint16_t transfer16(uint16_t data) = 0;
    virtual void transfer(void *buf, size_t count) = 0;

    // New transfer API.  If either send or recv == nullptr then ignore it
    virtual void transfer(const void *send, void *recv, size_t count) {
        const uint8_t *out = (const uint8_t *)send;
        uint8_t *in = (uint8_t *)recv;
        for (size_t i = 0; i < count; i++) {
            uint8_t t = out ? *(out++) : 0xff;
            t = transfer(t);
            if (in) {
                *(in++) = t;
            }
        }
    }

    // Transaction Functions
    virtual void usingInterrupt(int interruptNumber) = 0;
    virtual void notUsingInterrupt(int interruptNumber) = 0;
    virtual void beginTransaction(SPISettings settings) = 0;
    virtual void endTransaction(void) = 0;

    // SPI Configuration methods
    virtual void attachInterrupt() = 0;
    virtual void detachInterrupt() = 0;

    virtual void begin() = 0;
    virtual void end() = 0;
};

class SPIClassRP2040 : public HardwareSPI {
public:
    SPIClassRP2040(spi_inst_t *spi, pin_size_t rx, pin_size_t cs, pin_size_t sck, pin_size_t tx);

    // Send or receive 8- or 16-bit data.  Returns read back value
    byte transfer(uint8_t data) override;
    uint16_t transfer16(uint16_t data) override;

    // Sends buffer in 8 bit chunks.  Overwrites buffer with read data
    void transfer(void *buf, size_t count) override;

    // Sends one buffer and receives into another, much faster! can set rx or txbuf to nullptr
    void transfer(const void *txbuf, void *rxbuf, size_t count) override;

    // Call before/after every complete transaction
    void beginTransaction(SPISettings settings) override;
    void endTransaction(void) override;

    // Assign pins, call before begin()
    bool setRX(pin_size_t pin);
    bool setCS(pin_size_t pin);
    bool setSCK(pin_size_t pin);
    bool setTX(pin_size_t pin);

    // Call once to init/deinit SPI class, select pins, etc.
    virtual void begin() override {
        begin(false);
    }
    void begin(bool hwCS);
    void end() override;

    // Deprecated - do not use!
    void setBitOrder(BitOrder order) __attribute__((deprecated));
    void setDataMode(uint8_t uc_mode) __attribute__((deprecated));
    void setClockDivider(uint8_t uc_div) __attribute__((deprecated));

    // Unimplemented
    virtual void usingInterrupt(int interruptNumber) override {
        (void) interruptNumber;
    }
    virtual void notUsingInterrupt(int interruptNumber) override {
        (void) interruptNumber;
    }
    virtual void attachInterrupt() override { /* noop */ }
    virtual void detachInterrupt() override { /* noop */ }

private:
    spi_cpol_t cpol();
    spi_cpha_t cpha();
    uint8_t reverseByte(uint8_t b);
    uint16_t reverse16Bit(uint16_t w);
    void adjustBuffer(const void *s, void *d, size_t cnt, bool by16);

    spi_inst_t *_spi;
    SPISettings _spis;
    pin_size_t _RX, _TX, _SCK, _CS;
    bool _hwCS;
    bool _running; // SPI port active
    bool _initted; // Transaction begun
};

typedef SPIClassRP2040 SPIClass;

extern SPIClassRP2040 SPI;
extern SPIClassRP2040 SPI1;

// SPI
#define PIN_SPI0_MISO  (16u)
#define PIN_SPI0_MOSI  (19u)
#define PIN_SPI0_SCK   (18u)
#define PIN_SPI0_SS    (17u)

#define PIN_SPI1_MISO  (12u)
#define PIN_SPI1_MOSI  (15u)
#define PIN_SPI1_SCK   (14u)
#define PIN_SPI1_SS    (13u)
