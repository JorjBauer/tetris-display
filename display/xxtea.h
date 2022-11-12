#include <stdint.h>

int xxteaEncrypt(char *inputText, unsigned int inputTextLength, uint32_t const k[4]);
int xxteaDecrypt(char *inputText, unsigned int inputTextLength, uint32_t const k[4]);
