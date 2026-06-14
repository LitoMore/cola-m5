#include "cola_m5/text.h"

namespace cola_m5 {

String normalizeText(String text, bool preserveLineBreaks) {
  text.replace("\r", preserveLineBreaks ? "\n" : " ");
  text.replace("\n", preserveLineBreaks ? "\n" : " ");

  while (text.indexOf("  ") >= 0) {
    text.replace("  ", " ");
  }

  if (preserveLineBreaks) {
    while (text.indexOf(" \n") >= 0) {
      text.replace(" \n", "\n");
    }

    while (text.indexOf("\n ") >= 0) {
      text.replace("\n ", "\n");
    }

    while (text.indexOf("\n\n") >= 0) {
      text.replace("\n\n", "\n");
    }
  }

  text.trim();

  return text;
}

String compactText(String text, size_t maxChars) {
  text = normalizeText(text);

  if (text.length() <= maxChars) {
    return text;
  }

  if (maxChars <= 3) {
    return text.substring(0, maxChars);
  }

  return text.substring(0, maxChars - 3) + "...";
}

String tailText(const String& text, size_t maxChars) {
  if (text.length() <= maxChars) {
    return text;
  }

  if (maxChars <= 3) {
    return text.substring(text.length() - maxChars);
  }

  return "..." + text.substring(text.length() - (maxChars - 3));
}

size_t pageCountFor(const String& text, size_t charsPerPage) {
  if (text.length() == 0 || charsPerPage == 0) {
    return 1;
  }

  return (text.length() + charsPerPage - 1) / charsPerPage;
}

}  // namespace cola_m5
