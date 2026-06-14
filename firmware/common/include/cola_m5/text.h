#pragma once

#include <Arduino.h>

namespace cola_m5 {

String normalizeText(String text, bool preserveLineBreaks = false);
String compactText(String text, size_t maxChars);
String tailText(const String& text, size_t maxChars);
size_t pageCountFor(const String& text, size_t charsPerPage);

}  // namespace cola_m5
