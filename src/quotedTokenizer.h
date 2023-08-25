#ifndef QUOTEDTOKENIZER_H
#define QUOTEDTOKENIZER_H

bool isIn(char needle, const char* haystack);
char* quotedTokenizer(char* str, const char* delims, char** saveptr);

#endif