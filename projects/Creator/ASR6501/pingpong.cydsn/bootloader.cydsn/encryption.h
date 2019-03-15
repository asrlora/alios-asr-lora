#ifndef __ENCRYPTION_H__
#define __ENCRYPTION_H__
void init(unsigned char*s,unsigned char*key, unsigned long Len);
void crypt(unsigned char*s,unsigned char*Data,unsigned long Len);
int encryption(unsigned char*data,unsigned long dataLen,unsigned char * key,unsigned long keyLen);
#endif