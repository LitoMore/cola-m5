#pragma once

#include <Arduino.h>
#include <Print.h>
#include <SPI.h>

class XiaozhiEpaper : public Print {
public:
  bool begin();

  int width() const;
  int height() const;

  void waitDisplay();
  void startWrite();
  void endWrite();
  void display();

  void fillScreen(uint16_t color);
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint16_t color);
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color);
  void setTextSize(uint8_t size);
  void setTextColor(uint16_t color, uint16_t background);
  void setCursor(int32_t x, int32_t y);
  void setTextScroll(bool enabled);
  void setBrightness(uint8_t brightness);
  uint8_t getEpdMode() const;

  size_t write(uint8_t value) override;

private:
  static constexpr int kWidth = 176;
  static constexpr int kHeight = 264;
  static constexpr int kBytesPerRow = (kWidth + 7) / 8;
  static constexpr int kBufferLength = kBytesPerRow * kHeight;

  static constexpr int kPinBusy = 48;
  static constexpr int kPinReset = 47;
  static constexpr int kPinDc = 41;
  static constexpr int kPinCs = 42;
  static constexpr int kPinSck = 45;
  static constexpr int kPinMosi = 46;

  void hardwareReset();
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  void sendData(const uint8_t* data, size_t length);
  void setWindow(int xStart, int yStart, int xEnd, int yEnd);
  bool triggerRefresh(uint8_t mode);
  bool waitBusy(unsigned long timeoutMs);
  void drawPixel(int32_t x, int32_t y, bool ink);
  void drawVLine(int32_t x, int32_t y, int32_t h, bool ink);
  void drawChar(char value);
  bool isInkColor(uint16_t color) const;

  SPIClass spi_{FSPI};
  uint8_t buffer_[kBufferLength] = {};
  bool ready_ = false;
  bool firstRefresh_ = true;
  bool textInk_ = true;
  bool textBgInk_ = false;
  uint8_t textSize_ = 1;
  int32_t cursorX_ = 0;
  int32_t cursorY_ = 0;
};
