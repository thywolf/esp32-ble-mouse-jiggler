#include <quotedTokenizer.h>

/**
 * @brief Checks to see if a character is in a string.
 *
 * @param needle The character being searched for
 * @param haystack null-delimited string
 * @return true If found.
 */
bool isIn(char needle, const char* haystack) {
  const char* ptr = haystack;
  while (*ptr != 0) {
    if (needle == *ptr) {
      return true;
    }
    ptr++;
  }
  return false;
}

/**
 * @brief The tokenizer function that conforms to the SimpleSerialShell signature
 * requirement.  Please see strtok_r(3) for an explanation of the expected semantics.
 * 
 * @param str 
 * @param delims 
 * @param saveptr 
 * @return char* 
 */
char* quotedTokenizer(char* str, const char* delims, char** saveptr) {

  // Figure out where to start scanning from
  char* ptr = 0;
  if (str == 0) {
    ptr = *saveptr;
  } else {
    ptr = str;
  }
  
  // Consume/ignore any leading delimiters
  while (*ptr != 0 && isIn(*ptr, delims)) {
    ptr++;
  }

  // At this point we either have a null or a non-delimiter
  // character to deal with. If there is nothing left in the 
  // string then return 0
  if (*ptr == 0) {
    return 0;
  }

  char* result;

  // Check to see if this is a quoted token 
  if (*ptr == '\"') {
    // Skip the opening quote
    ptr++;
    // Result is the first eligible character
    result = ptr;
    while (*ptr != 0) {
      if (*ptr == '\"') {
        // Turn the trailing delimiter into a null-termination
        *ptr = 0;
        // Skip forward for next start
        ptr++;
        break;
      } 
      ptr++;
    }
  } 
  else {
    // Result is the first eligible character
    result = ptr;
    while (*ptr != 0) {
      if (isIn(*ptr, delims)) {
        // Turn the trailing delimiter into a null-termination
        *ptr = 0;
        // Skip forward for next start
        ptr++;
        break;
      } 
      ptr++;
    }
  }

  // We will start on the next character when we return
  *saveptr = ptr;
  return result;
}
