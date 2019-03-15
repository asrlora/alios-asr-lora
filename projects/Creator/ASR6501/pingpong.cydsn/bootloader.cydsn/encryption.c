#include "encryption.h"
#define SBOXLEN 256
void init(unsigned char*s,unsigned char*key, unsigned long Len)
{
    int i=0,j=0;
    unsigned char k[SBOXLEN]={0};
    unsigned char tmp=0;

    for(i=0;i<SBOXLEN;i++) {
        s[i]=i;
        k[i]=key[i%Len];
    }
    for(i=0;i<SBOXLEN;i++) {
        j=(j+s[i]+k[i])%SBOXLEN;
        tmp=s[i];
        s[i]=s[j];
        s[j]=tmp;
    }
}

void crypt(unsigned char*s,unsigned char*Data,unsigned long Len)
{
    int i=0,j=0,t=0;
    unsigned long k=0;
    unsigned char tmp;
    for(k=0;k<Len;k++)
    {
        i=(i+1)%SBOXLEN;
        j=(j+s[i])%SBOXLEN;
        tmp=s[i];
        s[i]=s[j];
        s[j]=tmp;
        t=(s[i]+s[j])%SBOXLEN;
        Data[k]^=s[t];
    }
}
int encryption(unsigned char*data,unsigned long dataLen,unsigned char * key,unsigned long keyLen)
{
    unsigned char s[SBOXLEN] = { 0 };
    if(!data||!key) return -1;
    init(s,key,keyLen);
    crypt(s,data,dataLen);
    return 0;
}

#if ENCRYPTION_SELF_TEST
#include <string.h>
#include <stdio.h>
#define DATA_LEN 64
int encryption_self_test()
{
    unsigned char s[SBOXLEN] = { 0 }, s2[SBOXLEN] = { 0 };
    char key[SBOXLEN] = { "justfortest\n" };
    char pData[DATA_LEN] = "This is test data to be encryptioned!\n";
    char pData2[DATA_LEN] = {0};
    unsigned long len = strlen(pData);
    int i;
 
    for(i=0;i<DATA_LEN;i++) pData2[i]=pData[i];
    printf("pData=%s\n", pData);
    printf("key=%s,length=%d\n\n", key, (int)strlen(key));
    init(s, (unsigned char*)key, strlen(key));
    printf("\n\n");
    for (i = 0; i<SBOXLEN; i++)
    {
        s2[i] = s[i];
    }
    printf("start to encryption:\n\n");
    crypt(s, (unsigned char*)pData, len);
    printf("after encrtption, pData=%s\n\n", pData);
    printf("start to decryption:\n\n");
    crypt(s2, (unsigned char*)pData, len);
    printf("after decryption, pData=%s\n\n", pData);
    if(strcmp(pData,pData2)!=0)
        return 1;
    return 0;
}
#endif