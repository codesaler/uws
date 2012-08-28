#ifndef __UWS_UTILS_H__
#define __UWS_UTILS_H__
#include "uws.h"
int wildcmp(const char*, const char*);
void setnonblocking(int sock);
char* strdup(const char *s);
char* strlcat(const char *s1, const char *s2);
char *itoa(const size_t data);
char* get_time_string();
int in_int_array(int array[], int needle, int length);
int gzcompress(char *zdata, size_t *nzdata, char *data, size_t ndata);
#endif
