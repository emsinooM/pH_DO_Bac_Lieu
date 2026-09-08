#pragma once

#ifdef __cplusplus
extern "C" {
#endif

unsigned int xcrc32(const unsigned char *buf, int len);
unsigned int xcrc32_non_poly(const unsigned char *buf, unsigned int preCrc, int len);

#ifdef __cplusplus
}
#endif
