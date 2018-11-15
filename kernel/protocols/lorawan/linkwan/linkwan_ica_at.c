/*
 * Copyright (C) 2015-2017 Alibaba Group Holding Limited
 */
#include <stdlib.h>
#include "hw.h"
#include "LoRaMac.h"
#include "Region.h"
#include "lwan_config.h"
#include "linkwan.h"
#include "low_power.h"
#include "linkwan_ica_at.h"
#include "lorawan_port.h"

#define ATCMD_SIZE (LORAWAN_APP_DATA_BUFF_SIZE + 14)
#define PORT_LEN 4

uint8_t atcmd[ATCMD_SIZE];
uint16_t atcmd_index = 0;
volatile bool g_atcmd_processing = false;
uint8_t g_default_key[LORA_KEY_LENGTH] = {0x41, 0x53, 0x52, 0x36, 0x35, 0x30, 0x58, 0x2D, 
                                          0x32, 0x30, 0x31, 0x38, 0x31, 0x30, 0x33, 0x30};

static linkwan_at_custom_handler_t linkwan_at_custom_handler = NULL;

static int hex2bin(const char *hex, uint8_t *bin, uint16_t bin_length)
{
    uint16_t hex_length = strlen(hex);
    const char *hex_end = hex + hex_length;
    uint8_t *cur = bin;
    uint8_t num_chars = hex_length & 1;
    uint8_t byte = 0;

    if ((hex_length + 1) / 2 > bin_length) {
        return -1;
    }

    while (hex < hex_end) {
        if ('A' <= *hex && *hex <= 'F') {
            byte |= 10 + (*hex - 'A');
        } else if ('a' <= *hex && *hex <= 'f') {
            byte |= 10 + (*hex - 'a');
        } else if ('0' <= *hex && *hex <= '9') {
            byte |= *hex - '0';
        } else {
            return -1;
        }
        hex++;
        num_chars++;

        if (num_chars >= 2) {
            num_chars = 0;
            *cur++ = byte;
            byte = 0;
        } else {
            byte <<= 4;
        }
    }
    return cur - bin;
}

// this can be in intrpt context
void linkwan_serial_input(uint8_t cmd)
{
    if(g_atcmd_processing) 
        return;
    
    if ((cmd >= '0' && cmd <= '9') || (cmd >= 'a' && cmd <= 'z') ||
        (cmd >= 'A' && cmd <= 'Z') || cmd == '?' || cmd == '+' ||
        cmd == ':' || cmd == '=' || cmd == ' ' || cmd == ',') {
        atcmd[atcmd_index++] = cmd;
    } else if (cmd == '\r' || cmd == '\n') {
        atcmd[atcmd_index] = '\0';
    }

    if (atcmd_index > ATCMD_SIZE) {
        atcmd_index = 0;
    }
}

int linkwan_serial_output(uint8_t *buffer, int len)
{
    PRINTF_AT("%s", buffer);
    linkwan_at_prompt_print();
    return 0;
}

void linkwan_at_prompt_print()
{
    PRINTF_RAW("\r\n%s%s:~# ", CONFIG_MANUFACTURER, CONFIG_DEVICE_MODEL);
}

void linkwan_at_process(void)
{
    bool ret = true;
    uint8_t length;
    uint8_t buf[16];
    uint8_t *rxcmd = atcmd + 2;
    int16_t rxcmd_index = atcmd_index - 2;

    if (atcmd_index <=2 && atcmd[atcmd_index] == '\0') {
        linkwan_at_prompt_print();
        atcmd_index = 0;
        memset(atcmd, 0xff, ATCMD_SIZE);
        return;
    }
    
    if (rxcmd_index <= 0 || rxcmd[rxcmd_index] != '\0') {
        return;
    }

#if 0    
    if(lwan_is_dev_busy()){
        PRINTF_RAW("\r\nDevice is busy now\r\n");
        atcmd_index = 0;
        memset(atcmd, 0xff, ATCMD_SIZE);
        return;
    }
#endif

    g_atcmd_processing = true;
    
    if (strncmp((const char *)rxcmd, LORA_AT_CJOINMODE, strlen(LORA_AT_CJOINMODE)) == 0) {
        uint8_t join_mode;
        if (rxcmd_index == (strlen(LORA_AT_CJOINMODE) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CJOINMODE)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"mode\"\r\nOK\r\n", LORA_AT_CJOINMODE);
        } else if (rxcmd_index == (strlen(LORA_AT_CJOINMODE) + 1) &&
                   rxcmd[strlen(LORA_AT_CJOINMODE)] == '?') {
            lwan_dev_config_get(DEV_CONFIG_JOIN_MODE, &join_mode);  
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CJOINMODE, join_mode);
        } else if (rxcmd_index == (strlen(LORA_AT_CJOINMODE) + 2) &&
                   rxcmd[strlen(LORA_AT_CJOINMODE)] == '=') {
            join_mode = strtol((const char *)&rxcmd[strlen(LORA_AT_CJOINMODE) + 1], NULL, 0);
            int res = lwan_dev_config_set(DEV_CONFIG_JOIN_MODE, (void *)&join_mode);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CDEVEUI, strlen(LORA_AT_CDEVEUI)) == 0) {
        if (rxcmd_index == (strlen(LORA_AT_CDEVEUI) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CDEVEUI)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=\"DevEUI:length is 16\"\r\n", LORA_AT_CDEVEUI);
        } else if (rxcmd_index == (strlen(LORA_AT_CDEVEUI) + 1) &&
                   rxcmd[strlen(LORA_AT_CDEVEUI)] == '?') {
            lwan_dev_keys_get(DEV_KEYS_OTA_DEVEUI, buf);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%02X%02X%02X%02X%02X%02X%02X%02X\r\nOK\r\n", \
                     LORA_AT_CDEVEUI, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
        } else if (rxcmd_index == (strlen(LORA_AT_CDEVEUI) + 17) &&
                   rxcmd[strlen(LORA_AT_CDEVEUI)] == '=') {
            ret = false;
            length = hex2bin((const char *)&rxcmd[strlen(LORA_AT_CDEVEUI) + 1], buf, LORA_EUI_LENGTH);
            if (length == LORA_EUI_LENGTH) {
                if(lwan_dev_keys_set(DEV_KEYS_OTA_DEVEUI, buf) == LWAN_SUCCESS) {
                    snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
                    ret = true;
                }
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CAPPEUI, strlen(LORA_AT_CAPPEUI)) == 0) {
        if (rxcmd_index == (strlen(LORA_AT_CAPPEUI) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CAPPEUI)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=\"AppEUI:length is 16\"\r\n", LORA_AT_CAPPEUI);
        } else if (rxcmd_index == (strlen(LORA_AT_CAPPEUI) + 1) &&
                   rxcmd[strlen(LORA_AT_CAPPEUI)] == '?') {
            lwan_dev_keys_get(DEV_KEYS_OTA_APPEUI, buf);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%02X%02X%02X%02X%02X%02X%02X%02X\r\nOK\r\n", \
                     LORA_AT_CAPPEUI, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
        } else if (rxcmd_index == (strlen(LORA_AT_CAPPEUI) + 17) &&
                   rxcmd[strlen(LORA_AT_CAPPEUI)] == '=') {
            ret = false;
            length = hex2bin((const char *)&rxcmd[strlen(LORA_AT_CAPPEUI) + 1], buf, LORA_EUI_LENGTH);
            if (length == LORA_EUI_LENGTH) {
                if(lwan_dev_keys_set(DEV_KEYS_OTA_APPEUI, buf) == LWAN_SUCCESS) {
                    snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
                    ret = true;
                }
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CAPPKEY, strlen(LORA_AT_CAPPKEY)) == 0) {
        if (rxcmd_index == (strlen(LORA_AT_CAPPKEY) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CAPPKEY)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=\"AppKey:length is 32\"\r\n", LORA_AT_CAPPKEY);
        } else if (rxcmd_index == (strlen(LORA_AT_CAPPKEY) + 1) &&
                   rxcmd[strlen(LORA_AT_CAPPKEY)] == '?') {
            lwan_dev_keys_get(DEV_KEYS_OTA_APPKEY, buf);   
            snprintf((char *)atcmd, ATCMD_SIZE,
                     "\r\n%s:%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\nOK\r\n", \
                     LORA_AT_CAPPKEY, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
                                      buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
        } else if (rxcmd_index == (strlen(LORA_AT_CAPPKEY) + 2*LORA_KEY_LENGTH + 1) &&
                   rxcmd[strlen(LORA_AT_CAPPKEY)] == '=') {
            ret = false;
            length = hex2bin((const char *)&rxcmd[strlen(LORA_AT_CAPPKEY) + 1], buf, LORA_KEY_LENGTH);
            if (length == LORA_KEY_LENGTH) {
                if(lwan_dev_keys_set(DEV_KEYS_OTA_APPKEY, buf) == LWAN_SUCCESS) {
                    snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
                    ret = true;
                }
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CDEVADDR, strlen(LORA_AT_CDEVADDR)) == 0) {
        uint8_t at_str_len;
        at_str_len = strlen(LORA_AT_CDEVADDR);
        if (rxcmd_index == (at_str_len + 2) &&
            strcmp((const char *)&rxcmd[at_str_len], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=<DevAddr:length is 8, Device address of ABP mode>\r\n", LORA_AT_CDEVADDR);
        } else if (rxcmd_index == (at_str_len + 1) &&
            rxcmd[at_str_len] == '?') {
            uint32_t devaddr;
            lwan_dev_keys_get(DEV_KEYS_ABP_DEVADDR, &devaddr);
            length = snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%08X\r\nOK\r\n", LORA_AT_CDEVADDR, (unsigned int)devaddr);
        } else if (rxcmd_index == (at_str_len + 9) &&
            rxcmd[at_str_len] == '=') {
            ret = false;
            length = hex2bin((const char *)&rxcmd[at_str_len + 1], buf, 4);
            if (length == 4) {
                uint32_t devaddr = buf[0] << 24 | buf[1] << 16 | buf[2] <<8 | buf[3];
                if(lwan_dev_keys_set(DEV_KEYS_ABP_DEVADDR, &devaddr) == LWAN_SUCCESS) {
                    snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
                    ret = true;
                }
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CAPPSKEY, strlen(LORA_AT_CAPPSKEY)) == 0) {
        uint8_t at_str_len;
        at_str_len = strlen(LORA_AT_CAPPSKEY);
        if (rxcmd_index == (at_str_len + 2) &&
            strcmp((const char *)&rxcmd[at_str_len], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=<AppSKey: length is 32>\r\n", LORA_AT_CAPPSKEY);
        } else if (rxcmd_index == (at_str_len + 1) &&
            rxcmd[at_str_len] == '?') {
            lwan_dev_keys_get(DEV_KEYS_ABP_APPSKEY, buf); 
            length = snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:", LORA_AT_CAPPSKEY);
            for (int i = 0; i < LORA_KEY_LENGTH; i++) {
                length += snprintf((char *)(atcmd + length), ATCMD_SIZE, "%02X", buf[i]);
            }
            snprintf((char *)(atcmd + length), ATCMD_SIZE, "\r\nOK\r\n");
        } else if (rxcmd_index == (at_str_len + 2*LORA_KEY_LENGTH + 1) &&
            rxcmd[at_str_len] == '=') {
            ret = false;
            length = hex2bin((const char *)&rxcmd[at_str_len + 1], buf, LORA_KEY_LENGTH);
            if (length == LORA_KEY_LENGTH) {
                if(lwan_dev_keys_set(DEV_KEYS_ABP_APPSKEY, buf) == LWAN_SUCCESS) {
                    snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
                    ret = true;
                }
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CNWKSKEY, strlen(LORA_AT_CNWKSKEY)) == 0) {
        uint8_t at_str_len;
        at_str_len = strlen(LORA_AT_CNWKSKEY);
        if (rxcmd_index == (at_str_len + 2) &&
            strcmp((const char *)&rxcmd[at_str_len], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=<NwkSKey:length is 32>\r\n", LORA_AT_CNWKSKEY);
        } else if (rxcmd_index == (at_str_len + 1) &&
            rxcmd[at_str_len] == '?') {
            lwan_dev_keys_get(DEV_KEYS_ABP_NWKSKEY, buf); 
            length = snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:", LORA_AT_CNWKSKEY);
            for (int i = 0; i < LORA_KEY_LENGTH; i++) {
                length += snprintf((char *)(atcmd + length), ATCMD_SIZE, "%02X", buf[i]);
            }
            snprintf((char *)(atcmd + length), ATCMD_SIZE, "\r\nOK\r\n");
        } else if (rxcmd_index == (at_str_len + 2*LORA_KEY_LENGTH + 1) &&
            rxcmd[at_str_len] == '=') {
            ret = false;
            length = hex2bin((const char *)&rxcmd[at_str_len + 1], buf, LORA_KEY_LENGTH);
            if (length == LORA_KEY_LENGTH) {
                if(lwan_dev_keys_set(DEV_KEYS_ABP_NWKSKEY, buf) == LWAN_SUCCESS) {
                    snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
                    ret = true;
                }
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CADDMUTICAST, strlen(LORA_AT_CADDMUTICAST)) == 0) {
        if (rxcmd_index == (strlen(LORA_AT_CADDMUTICAST) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CADDMUTICAST)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"DevAddr\",\"AppSKey\",\"NwkSKey\"\r\nOK\r\n", LORA_AT_CADDMUTICAST);
        } else if (rxcmd_index > (strlen(LORA_AT_CADDMUTICAST) + 1) &&
                   rxcmd[strlen(LORA_AT_CADDMUTICAST)] == '=') {
            MulticastParams_t curMulticastInfo;
            char *str = strtok_l((char *)&rxcmd[strlen(LORA_AT_CADDMUTICAST) + 1], ",");
            curMulticastInfo.Address = (uint32_t)strtoul(str, NULL, 16);
            str = strtok_l((char *)NULL, ",");
            uint8_t len = hex2bin((const char *)str, curMulticastInfo.AppSKey, 16);
            str = strtok_l((char *)NULL, ",");
            len = hex2bin((const char *)str, curMulticastInfo.NwkSKey, 16);
            str = strtok_l((char *)NULL, ",");
            curMulticastInfo.Periodicity = strtoul(str, NULL, 16);
            str = strtok_l((char *)NULL, ",");
            curMulticastInfo.Datarate = strtoul(str, NULL, 16);
            curMulticastInfo.Frequency = 0;
            if (len == 16) {
                ret = lwan_multicast_add(&curMulticastInfo);
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            } else {
	            ret = false;
            }
	    }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CDELMUTICAST, strlen(LORA_AT_CDELMUTICAST)) == 0) {
        if (rxcmd_index == (strlen(LORA_AT_CDELMUTICAST) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CDELMUTICAST)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"DevAddr\"\r\nOK\r\n", LORA_AT_CDELMUTICAST);
        } else if (rxcmd_index > (strlen(LORA_AT_CDELMUTICAST) + 1) &&
                   rxcmd[strlen(LORA_AT_CDELMUTICAST)] == '=') {
            uint32_t devAddr = (uint32_t)strtol((const char *)&rxcmd[strlen(LORA_AT_CDELMUTICAST) + 1], NULL, 16);
            ret = lwan_multicast_del(devAddr);
            if (ret == true) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            } else {
                ret = false;
	        }
	    } else {
	        ret = false;
	    }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CNUMMUTICAST, strlen(LORA_AT_CNUMMUTICAST)) == 0) {
	   if (rxcmd_index == (strlen(LORA_AT_CNUMMUTICAST) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CNUMMUTICAST)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"number\"\r\nOK\r\n", LORA_AT_CNUMMUTICAST);
        } else if (rxcmd_index == (strlen(LORA_AT_CDELMUTICAST) + 1) &&
                   rxcmd[strlen(LORA_AT_CDELMUTICAST)] == '?') {
            uint8_t multiNum = lwan_multicast_num_get();
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%u\r\nOK\r\n", LORA_AT_CNUMMUTICAST, multiNum);
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CFREQBANDMASK, strlen(LORA_AT_CFREQBANDMASK)) == 0) {
        uint16_t freqband_mask;
        if (rxcmd_index == (strlen(LORA_AT_CFREQBANDMASK) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CFREQBANDMASK)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=\"mask\"\r\nOK\r\n", LORA_AT_CFREQBANDMASK);
        } else if (rxcmd_index == (strlen(LORA_AT_CFREQBANDMASK) + 1) &&
                   rxcmd[strlen(LORA_AT_CFREQBANDMASK)] == '?') {
            lwan_dev_config_get(DEV_CONFIG_FREQBAND_MASK, &freqband_mask);  
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%04X\r\nOK\r\n", LORA_AT_CFREQBANDMASK, freqband_mask);
        } else if (rxcmd_index > (strlen(LORA_AT_CFREQBANDMASK) + 1) &&
                   rxcmd[strlen(LORA_AT_CFREQBANDMASK)] == '=') {
            uint8_t mask[2];
            length = hex2bin((const char *)&rxcmd[strlen(LORA_AT_CFREQBANDMASK) + 1], (uint8_t *)mask, 2);
            if (length == 2) {
                freqband_mask = mask[1] | ((uint16_t)mask[0] << 8);
                int res = lwan_dev_config_set(DEV_CONFIG_FREQBAND_MASK, (void *)&freqband_mask);
                if (res == LWAN_SUCCESS) {
                    snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
                }else {
                    ret = false;
                }
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CULDLMODE, strlen(LORA_AT_CULDLMODE)) == 0) {
        uint8_t mode;
        if (rxcmd_index == (strlen(LORA_AT_CULDLMODE) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CULDLMODE)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=\"mode\"\r\nOK\r\n", LORA_AT_CULDLMODE);
        } else if (rxcmd_index == (strlen(LORA_AT_CULDLMODE) + 1) &&
                   rxcmd[strlen(LORA_AT_CULDLMODE)] == '?') {
            lwan_dev_config_get(DEV_CONFIG_ULDL_MODE, &mode);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CULDLMODE, mode);
        } else if (rxcmd_index == (strlen(LORA_AT_CULDLMODE) + 2) &&
                   rxcmd[strlen(LORA_AT_CULDLMODE)] == '=') {
            mode = strtol((const char *)&rxcmd[strlen(LORA_AT_CULDLMODE) + 1], NULL, 0);
            int res = lwan_dev_config_set(DEV_CONFIG_ULDL_MODE, (void *)&mode);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CWORKMODE, strlen(LORA_AT_CWORKMODE)) == 0) {
        uint8_t mode;
        if (rxcmd_index == (strlen(LORA_AT_CWORKMODE) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CWORKMODE)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=\"mode\"\r\nOK\r\n", LORA_AT_CWORKMODE);
        } else if (rxcmd_index == (strlen(LORA_AT_CWORKMODE) + 1) &&
                   rxcmd[strlen(LORA_AT_CWORKMODE)] == '?') {
            lwan_dev_config_get(DEV_CONFIG_WORK_MODE, &mode);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CWORKMODE, mode);
        } else if (rxcmd_index == (strlen(LORA_AT_CWORKMODE) + 2) &&
                   rxcmd[strlen(LORA_AT_CWORKMODE)] == '=') {
            mode = strtol((const char *)&rxcmd[strlen(LORA_AT_CWORKMODE) + 1], NULL, 0);
            if (mode == 2) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            } else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CREPEATERFREQ, strlen(LORA_AT_CREPEATERFREQ)) == 0) {
        ret = false;
    } else if (strncmp((const char *)rxcmd, LORA_AT_CCLASS, strlen(LORA_AT_CCLASS)) == 0) {
        uint8_t class;
        if (rxcmd_index == (strlen(LORA_AT_CCLASS) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CCLASS)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE,
                     "\r\n%s:\"class\",\"branch\",\"para1\",\"para2\",\"para3\",\"para4\"\r\nOK\r\n",
                     LORA_AT_CCLASS);
        } else if (rxcmd_index == (strlen(LORA_AT_CCLASS) + 1) &&
                   rxcmd[strlen(LORA_AT_CCLASS)] == '?') {
            lwan_dev_config_get(DEV_CONFIG_CLASS, &class);  
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CCLASS, class);
        } else if (rxcmd_index >= (strlen(LORA_AT_CCLASS) + 2) &&
                   rxcmd[strlen(LORA_AT_CCLASS)] == '=') {
            int8_t branch = -1;
            uint32_t param1 = 0;
            uint32_t param2 = 0;
            uint32_t param3 = 0;
            uint32_t param4 = 0;
            char *str = strtok_l((char *)&rxcmd[strlen(LORA_AT_CCLASS) + 1], ",");
            class = strtol((const char *)str, NULL, 0);
            if(class == CLASS_B) {
                str = strtok_l((char *)NULL, ",");
                if(str) {
                    branch = strtol((const char *)str, NULL, 0);
                    str = strtok_l((char *)NULL, ",");
                    param1 = strtol((const char *)str, NULL, 0);
                    if(branch == 1 ){
                        str = strtok_l((char *)NULL, ",");
                        param2 = strtol((const char *)str, NULL, 0);
                        str = strtok_l((char *)NULL, ",");
                        param3 = strtol((const char *)str, NULL, 0);
                        str = strtok_l((char *)NULL, ",");
                        param4 = strtol((const char *)str, NULL, 0);
                    }
                }
            }
            int res = lwan_dev_config_set(DEV_CONFIG_CLASS, (void *)&class);
            if (res == LWAN_SUCCESS) {
                ClassBParam_t classb_param;
                lwan_dev_config_get(DEV_CONFIG_CLASSB_PARAM, (void *)&classb_param);
                if(branch==0){
                    classb_param.periodicity = param1;
                }else{
                    classb_param.beacon_freq = param1;
                    classb_param.beacon_dr = param2;
                    classb_param.pslot_freq = param3;
                    classb_param.beacon_dr = param4;
                }
                res = lwan_dev_config_set(DEV_CONFIG_CLASSB_PARAM, (void *)&classb_param);
                if (res == LWAN_SUCCESS) {
                    snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
                }else{
                    ret = false;
                }
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CBL, strlen(LORA_AT_CBL)) == 0) {
        if (rxcmd_index == (strlen(LORA_AT_CBL) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CBL)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"value\"\r\nOK\r\n", LORA_AT_CBL);
        } else if (rxcmd_index == (strlen(LORA_AT_CBL) + 1) &&
                   rxcmd[strlen(LORA_AT_CBL)] == '?') {
            uint8_t batteryLevel = lwan_dev_battery_get();
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CBL, batteryLevel);
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CSTATUS, strlen(LORA_AT_CSTATUS)) == 0) {
        int status;
        if (rxcmd_index == (strlen(LORA_AT_CSTATUS) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CSTATUS)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"status\"\r\nOK\r\n", LORA_AT_CSTATUS);
        } else if (rxcmd_index == (strlen(LORA_AT_CSTATUS) + 1) &&
                   rxcmd[strlen(LORA_AT_CSTATUS)] == '?') {
            ret = true;
            status = lwan_dev_status_get();
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%02d\r\nOK\r\n", LORA_AT_CSTATUS, status);
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CJOIN, strlen(LORA_AT_CJOIN)) == 0) {
        uint8_t bJoin, bAutoJoin;
        uint16_t joinInterval, joinRetryCnt;
        if (rxcmd_index == (strlen(LORA_AT_CJOIN) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CJOIN)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:<ParaTag1>,[ParaTag2],[ParaTag3],[ParaTag4]\r\nOK\r\n", LORA_AT_CJOIN);
        } else if (rxcmd_index == (strlen(LORA_AT_CJOIN) + 1) &&
                   rxcmd[strlen(LORA_AT_CJOIN)] == '?') {
            JoinSettings_t join_settings;
            lwan_dev_config_get(DEV_CONFIG_JOIN_SETTINGS, &join_settings);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d,%d,%d,%d\r\nOK\r\n", LORA_AT_CJOIN, join_settings.auto_join, 
                     join_settings.auto_join, join_settings.join_interval, join_settings.join_trials);
        } else if (rxcmd_index >=  (strlen(LORA_AT_CJOIN) + 2) &&
                   rxcmd[strlen(LORA_AT_CJOIN)] == '=') {              
            char *str = strtok_l((char *)&rxcmd[strlen(LORA_AT_CJOIN) + 1], ",");
            bJoin = strtol((const char *)str, NULL, 0);
            str = strtok_l((char *)NULL, ",");
            bAutoJoin = strtol((const char *)str, NULL, 0);
            str = strtok_l((char *)NULL, ",");
            joinInterval = strtol((const char *)str, NULL, 0);
            str = strtok_l((char *)NULL, ",");
            joinRetryCnt = strtol((const char *)str, NULL, 0);
            
            int res = lwan_join(bJoin, bAutoJoin, joinInterval, joinRetryCnt);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }      
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_DTRX, strlen(LORA_AT_DTRX)) == 0) {
       if (rxcmd_index == (strlen(LORA_AT_DTRX) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_DTRX)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:[confirm],[nbtrials],[length],<payload>\r\nOK\r\n", LORA_AT_DTRX);
        } else if (rxcmd_index >= (strlen(LORA_AT_DTRX) + 2) &&
                   rxcmd[strlen(LORA_AT_DTRX)] == '=') {
            uint8_t confirm, Nbtrials, len;
            int bin_len = 0;
            uint8_t payload[LORAWAN_APP_DATA_BUFF_SIZE];
            char *str = strtok_l((char *)&rxcmd[strlen(LORA_AT_DTRX) + 1], ",");
            confirm = strtol((const char *)str, NULL, 0);
            str = strtok_l((char *)NULL, ",");
            Nbtrials = strtol((const char *)str, NULL, 0);
            str = strtok_l((char *)NULL, ",");
            len = strtol((const char *)str, NULL, 0);
            str = strtok_l((char *)NULL, ",");
            bin_len = hex2bin((const char *)str, payload, len);
            
            if(bin_len>=0) {
                int res = lwan_data_send(confirm, Nbtrials, payload, bin_len);
                if (res == LWAN_SUCCESS) {
                    snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK+SEND:%02X\r\n", bin_len);
                }else{
                    snprintf((char *)atcmd, ATCMD_SIZE, "\r\nERR+SEND:00\r\n");
                }
            }else{
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_DRX, strlen(LORA_AT_DRX)) == 0) {
        if (rxcmd_index == (strlen(LORA_AT_DRX) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_DRX)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:<Length>,<Payload>\r\nOK\r\n", LORA_AT_DRX);
        } else if (rxcmd_index == (strlen(LORA_AT_DRX) + 1) &&
                   rxcmd[strlen(LORA_AT_DRX)] == '?') {
            uint8_t port;
            uint8_t *buf;
            uint8_t size;
                    
            int res = lwan_data_recv(&port, &buf, &size);
            if(res == LWAN_SUCCESS) {
                int16_t len = snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d", LORA_AT_DRX, size);
                if (size > 0) {
                    len += snprintf((char *)(atcmd + len), ATCMD_SIZE, ",");
                    for(int i=0; i<size; i++) {
                        len += snprintf((char *)(atcmd + len), ATCMD_SIZE, "%02X", buf[i]);
                    }
                }
                snprintf((char *)(atcmd + len), ATCMD_SIZE, "\r\n%s\r\n", "OK");
            }else{
                ret = false;
            }
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CCONFIRM, strlen(LORA_AT_CCONFIRM)) == 0) {
        uint8_t cfm;
        if (rxcmd_index == (strlen(LORA_AT_CCONFIRM) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CCONFIRM)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"value\"\r\nOK\r\n", LORA_AT_CCONFIRM);
        } else if (rxcmd_index == (strlen(LORA_AT_CCONFIRM) + 1) &&
                   rxcmd[strlen(LORA_AT_CCONFIRM)] == '?') {
            lwan_mac_config_get(MAC_CONFIG_CONFIRM_MSG, &cfm);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CCONFIRM, cfm);
        } else if (rxcmd_index == (strlen(LORA_AT_CCONFIRM) + 2) &&
                   rxcmd[strlen(LORA_AT_CCONFIRM)] == '=') {
            cfm = strtol((const char *)&rxcmd[strlen(LORA_AT_CCONFIRM) + 1], NULL, 0);
            int res = lwan_mac_config_set(MAC_CONFIG_CONFIRM_MSG, (void *)&cfm);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CAPPPORT, strlen(LORA_AT_CAPPPORT)) == 0) {
        uint8_t port;
        uint8_t at_cmd_len;
        at_cmd_len = strlen(LORA_AT_CAPPPORT);
        if (rxcmd_index == (at_cmd_len + 2) &&
            strcmp((const char *)&rxcmd[at_cmd_len], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"value\"\r\nOK\r\n", LORA_AT_CAPPPORT);
        } else if (rxcmd_index == (at_cmd_len + 1) &&
                   rxcmd[at_cmd_len] == '?') {
            lwan_mac_config_get(MAC_CONFIG_APP_PORT, &port);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CAPPPORT, port);
        } else if (rxcmd_index > (at_cmd_len + 1) &&
                   rxcmd[at_cmd_len] == '=') {
            port = (uint8_t)strtol((const char *)&rxcmd[at_cmd_len + 1], NULL, 0);
            int res = lwan_mac_config_set(MAC_CONFIG_APP_PORT, (void *)&port);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CDATARATE, strlen(LORA_AT_CDATARATE)) == 0) {
        uint8_t datarate;
        if (rxcmd_index == (strlen(LORA_AT_CDATARATE) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CDATARATE)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"value\"\r\nOK\r\n", LORA_AT_CDATARATE);
        } else if (rxcmd_index == (strlen(LORA_AT_CDATARATE) + 1) &&
                   rxcmd[strlen(LORA_AT_CDATARATE)] == '?') {
            lwan_mac_config_get(MAC_CONFIG_DATARATE, &datarate);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CDATARATE, datarate);
        } else if (rxcmd_index == (strlen(LORA_AT_CDATARATE) + 2) &&
                   rxcmd[strlen(LORA_AT_CDATARATE)] == '=') {
            datarate = strtol((const char *)&rxcmd[strlen(LORA_AT_CDATARATE) + 1], NULL, 0);
            int res = lwan_mac_config_set(MAC_CONFIG_DATARATE, (void *)&datarate);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CRSSI, strlen(LORA_AT_CRSSI)) == 0) {
	if (rxcmd_index == (strlen(LORA_AT_CRSSI) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CRSSI)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s\r\nOK\r\n", LORA_AT_CRSSI);
        } else if ((rxcmd_index > strlen(LORA_AT_CRSSI) + 2) &&
                   (rxcmd[rxcmd_index - 1] == '?')) {
            if (rxcmd_index - strlen(LORA_AT_CRSSI) - 2 == 0) {
                ret = false;
            } else {
                char freq_str[4];
                int16_t channel_rssi[8];
                strncpy(freq_str, (const char *)&rxcmd[strlen(LORA_AT_CRSSI)], rxcmd_index - strlen(LORA_AT_CRSSI) - 1);
                freq_str[rxcmd_index - strlen(LORA_AT_CRSSI) - 1] = '\0';
                uint8_t freq_band = strtol((const char *)freq_str, NULL, 0);
                lwan_dev_rssi_get(freq_band, channel_rssi);
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\r\n0:%d\r\n1:%d\r\n2:%d\r\n3:%d\r\n4:%d\r\n5:%d\r\n6:%d\r\n7:%d\r\nOK\r\n", \
                         LORA_AT_CRSSI, channel_rssi[0], channel_rssi[1], channel_rssi[2], channel_rssi[3], channel_rssi[4], channel_rssi[5],
                         channel_rssi[6], channel_rssi[7]);
                ret = true;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CNBTRIALS, strlen(LORA_AT_CNBTRIALS)) == 0) {
	    uint8_t m_type, value;
        if (rxcmd_index == (strlen(LORA_AT_CNBTRIALS) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CNBTRIALS)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"MTypes\",\"value\"\r\nOK\r\n", LORA_AT_CNBTRIALS);
        } else if (rxcmd_index == (strlen(LORA_AT_CNBTRIALS) + 1) &&
                   rxcmd[strlen(LORA_AT_CNBTRIALS)] == '?') {
            lwan_mac_config_get(MAC_CONFIG_CONFIRM_MSG, &m_type);
            if(m_type)
                lwan_mac_config_get(MAC_CONFIG_CONF_NBTRIALS, &value);
            else
                lwan_mac_config_get(MAC_CONFIG_UNCONF_NBTRIALS, &value);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d,%d\r\nOK\r\n", LORA_AT_CNBTRIALS, m_type, value);
        } else if (rxcmd_index >  (strlen(LORA_AT_CNBTRIALS) + 1) &&
                   rxcmd[strlen(LORA_AT_CNBTRIALS)] == '=') {
            m_type = rxcmd[strlen(LORA_AT_CNBTRIALS) + 1] - '0';
            value = strtol((const char *)&rxcmd[strlen(LORA_AT_CNBTRIALS) + 3], NULL, 0);
            int res = LWAN_SUCCESS;
            if(m_type)
                res = lwan_mac_config_set(MAC_CONFIG_CONF_NBTRIALS, (void *)&value);   
            else
                res = lwan_mac_config_set(MAC_CONFIG_UNCONF_NBTRIALS, (void *)&value);
            if (res == LWAN_SUCCESS)    
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            else
                ret = false;
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CRM, strlen(LORA_AT_CRM)) == 0) {
        uint8_t reportMode;
        uint16_t reportInterval;
        if (rxcmd_index == (strlen(LORA_AT_CRM) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CRM)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"reportMode\",\"reportInterval\"\r\nOK\r\n", LORA_AT_CRM);
        } else if (rxcmd_index ==  (strlen(LORA_AT_CRM) + 1) &&
                   rxcmd[strlen(LORA_AT_CRM)] == '?') {
            lwan_mac_config_get(MAC_CONFIG_REPORT_MODE, &reportMode);
            lwan_mac_config_get(MAC_CONFIG_REPORT_INTERVAL, &reportInterval);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d,%u\r\nOK\r\n", LORA_AT_CRM, reportMode, (unsigned int)reportInterval);
        } else if (rxcmd_index >=  (strlen(LORA_AT_CRM) + 2) &&
                   rxcmd[strlen(LORA_AT_CRM)] == '=') {
            reportMode = rxcmd[strlen(LORA_AT_CRM) + 1] - '0';
            reportInterval = strtol((const char *)&rxcmd[strlen(LORA_AT_CRM) + 3], NULL, 0);
            lwan_mac_config_set(MAC_CONFIG_REPORT_MODE, (void *)&reportMode);
            int res = lwan_mac_config_set(MAC_CONFIG_REPORT_INTERVAL, (void *)&reportInterval);   
            if (res == LWAN_SUCCESS)    
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            else
                ret = false;
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CTXP, strlen(LORA_AT_CTXP)) == 0) {
        uint8_t tx_power;
        if (rxcmd_index == (strlen(LORA_AT_CTXP) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CTXP)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"value\"\r\nOK\r\n", LORA_AT_CTXP);
        } else if (rxcmd_index == (strlen(LORA_AT_CTXP) + 1) &&
                   rxcmd[strlen(LORA_AT_CTXP)] == '?') {            
            lwan_mac_config_get(MAC_CONFIG_TX_POWER, &tx_power);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CTXP, tx_power);
        } else if (rxcmd_index == (strlen(LORA_AT_CTXP) + 2) &&
                   rxcmd[strlen(LORA_AT_CTXP)] == '=') {
            tx_power = strtol((const char *)&rxcmd[strlen(LORA_AT_CTXP) + 1], NULL, 0);
            int res = lwan_mac_config_set(MAC_CONFIG_TX_POWER, (void *)&tx_power);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CLINKCHECK, strlen(LORA_AT_CLINKCHECK)) == 0) {
        uint8_t checkValue;
        if (rxcmd_index == (strlen(LORA_AT_CLINKCHECK) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CLINKCHECK)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"value\"\r\nOK\r\n", LORA_AT_CLINKCHECK);
        } else if (rxcmd_index > (strlen(LORA_AT_CLINKCHECK) + 1) &&
                   rxcmd[strlen(LORA_AT_CLINKCHECK)] == '=') {
            checkValue = strtol((const char *)&rxcmd[strlen(LORA_AT_CLINKCHECK) + 1], NULL, 0);
            int res = lwan_mac_config_set(MAC_CONFIG_CHECK_MODE, (void *)&checkValue);
            if (res == LWAN_SUCCESS) {
                if(checkValue==1)
                    lwan_mac_req_send(MAC_REQ_LINKCHECK, 0);
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CADR, strlen(LORA_AT_CADR)) == 0) {
        uint8_t adr;
        if (rxcmd_index == (strlen(LORA_AT_CADR) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CADR)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"value\"\r\nOK\r\n", LORA_AT_CADR);
        } else if (rxcmd_index == (strlen(LORA_AT_CADR) + 1) &&
                   rxcmd[strlen(LORA_AT_CADR)] == '?') {
            lwan_mac_config_get(MAC_CONFIG_ADR_ENABLE, &adr);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CADR, adr);
        } else if (rxcmd_index == (strlen(LORA_AT_CADR) + 2) &&
                   rxcmd[strlen(LORA_AT_CADR)] == '=') {
            adr = strtol((const char *)&rxcmd[strlen(LORA_AT_CADR) + 1], NULL, 0);
            int res = lwan_mac_config_set(MAC_CONFIG_ADR_ENABLE, (void *)&adr);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CRXP, strlen(LORA_AT_CRXP)) == 0) {
        RxParams_t rx_params;
        if (rxcmd_index == (strlen(LORA_AT_CRXP) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CRXP)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"RX1DRoffset\",\"RX2DataRate\",\"RX2Frequency\"\r\nOK\r\n", LORA_AT_CRXP);
        } else if (rxcmd_index == (strlen(LORA_AT_CRXP) + 1) &&
                   rxcmd[strlen(LORA_AT_CADR)] == '?') {
            lwan_mac_config_get(MAC_CONFIG_RX_PARAM, &rx_params);  
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%u,%u,%u\r\nOK\r\n", LORA_AT_CRXP, rx_params.rx1_dr_offset, rx_params.rx2_dr, (unsigned int)rx_params.rx2_freq);            
        } else if (rxcmd_index > (strlen(LORA_AT_CRXP) + 2) &&
                   rxcmd[strlen(LORA_AT_CRXP)] == '=') {
            uint8_t rx1_dr_offset;
            uint8_t rx2_dr;
            uint32_t rx2_freq;                            
            char *str = strtok_l((char *)&rxcmd[strlen(LORA_AT_CRXP) + 1], ",");
            rx1_dr_offset = strtol((const char *)str, NULL, 0);
            str = strtok_l((char *)NULL, ",");
            rx2_dr = strtol((const char *)str, NULL, 0);
            str = strtok_l((char *)NULL, ",");
            rx2_freq = strtol((const char *)str, NULL, 0);
            rx_params.rx1_dr_offset = rx1_dr_offset;
            rx_params.rx2_dr = rx2_dr;
            rx_params.rx2_freq = rx2_freq;
            int res = lwan_mac_config_set(MAC_CONFIG_RX_PARAM, (void *)&rx_params);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }          
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CFREQLIST, strlen(LORA_AT_CFREQLIST)) == 0) {
        ret = false;
    } else if (strncmp((const char *)rxcmd, LORA_AT_CRX1DELAY, strlen(LORA_AT_CRX1DELAY)) == 0) {
	    uint16_t rx1delay;
        if (rxcmd_index == (strlen(LORA_AT_CRX1DELAY) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CRX1DELAY)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"value\"\r\nOK\r\n", LORA_AT_CRX1DELAY);
        } else if (rxcmd_index == (strlen(LORA_AT_CRX1DELAY) + 1) &&
                   rxcmd[strlen(LORA_AT_CRX1DELAY)] == '?') {
            lwan_mac_config_get(MAC_CONFIG_RX1_DELAY, &rx1delay);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%u\r\nOK\r\n", LORA_AT_CRX1DELAY, (unsigned int)rx1delay);
        } else if (rxcmd_index == (strlen(LORA_AT_CRX1DELAY) + 2) &&
                   rxcmd[strlen(LORA_AT_CRX1DELAY)] == '=') {
            rx1delay = strtol((const char *)&rxcmd[strlen(LORA_AT_CRX1DELAY) + 1], NULL, 0);
            int res = lwan_mac_config_set(MAC_CONFIG_RX1_DELAY, (void *)&rx1delay);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CSAVE, strlen(LORA_AT_CSAVE)) == 0) {
        if (rxcmd_index == (strlen(LORA_AT_CSAVE) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CSAVE)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s\r\nOK\r\n", LORA_AT_CSAVE);
        } else if (rxcmd_index == strlen(LORA_AT_CSAVE)) {
            int res = lwan_mac_config_save(); 
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CRESTORE, strlen(LORA_AT_CRESTORE)) == 0) {
        if (rxcmd_index == (strlen(LORA_AT_CRESTORE) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CRESTORE)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s\r\nOK\r\n", LORA_AT_CSAVE);
        } else if (rxcmd_index == strlen(LORA_AT_CRESTORE)) {
            LWanMacConfig_t default_mac_config = LWAN_MAC_CONFIG_DEFAULT;
            int res = lwan_mac_config_reset(&default_mac_config); 
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }       
    } else if (strncmp((const char *)rxcmd, LORA_AT_PINGSLOTINFOREQ, strlen(LORA_AT_PINGSLOTINFOREQ)) == 0){
        if (rxcmd_index == (strlen(LORA_AT_PINGSLOTINFOREQ) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_PINGSLOTINFOREQ)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:<periodicity>\r\nOK\r\n", LORA_AT_PINGSLOTINFOREQ);
        } else if (rxcmd_index == (strlen(LORA_AT_PINGSLOTINFOREQ) + 1) &&
                   rxcmd[strlen(LORA_AT_PINGSLOTINFOREQ)] == '?') {
            ClassBParam_t classb_param;
            lwan_dev_config_get(DEV_CONFIG_CLASSB_PARAM, &classb_param);
            snprintf((char *)atcmd,ATCMD_SIZE,"\r\n%u\r\nOK\r\n",classb_param.periodicity);
        } else if (rxcmd_index >= (strlen(LORA_AT_PINGSLOTINFOREQ)+2)&&
                    rxcmd[strlen(LORA_AT_PINGSLOTINFOREQ)] == '='){
            char * strPeriodicity = (char *)&(rxcmd[strlen(LORA_AT_PINGSLOTINFOREQ)+1]);
            uint8_t periodicityVal = (uint8_t)strtol((const char *)strPeriodicity,NULL,0);
            int res = lwan_mac_req_send(MAC_REQ_PSLOT_INFO, &periodicityVal);
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else {
                ret = false;
            }
        } else {
            ret = false;
        }        
    } else if (strncmp((const char *)rxcmd, LORA_AT_CKEYSPROTECT, strlen(LORA_AT_CKEYSPROTECT)) == 0) {
        uint8_t at_str_len;
        at_str_len = strlen(LORA_AT_CKEYSPROTECT);
        if (rxcmd_index == (at_str_len + 2) &&
            strcmp((const char *)&rxcmd[at_str_len], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=<ProtectKey:length is 32>\r\n", LORA_AT_CKEYSPROTECT);
        } else if (rxcmd_index == (at_str_len + 1) &&
            rxcmd[at_str_len] == '?') {
            lwan_dev_keys_get(DEV_KEYS_PKEY, buf);
            bool protected = lwan_is_key_valid(buf, LORA_KEY_LENGTH);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_CKEYSPROTECT, protected);
        } else if (rxcmd[at_str_len] == '=') {
            int res = LWAN_SUCCESS;
            length = hex2bin((const char *)&rxcmd[at_str_len + 1], buf, LORA_KEY_LENGTH);
            if (length == LORA_KEY_LENGTH) {
                lwan_dev_keys_set(DEV_KEYS_PKEY, buf);
            } else {
                lwan_dev_keys_set(DEV_KEYS_PKEY, g_default_key);
            }
            
            if (res == LWAN_SUCCESS) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            }else{
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CLPM, strlen(LORA_AT_CLPM)) == 0) {
	    if (rxcmd_index == (strlen(LORA_AT_CLPM) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_CLPM)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"Mode\"\r\nOK\r\n", LORA_AT_CLPM);
        } else if (rxcmd_index == (strlen(LORA_AT_CLPM) + 2) &&
                   rxcmd[strlen(LORA_AT_CLPM)] == '=') {
            int8_t mode = strtol((const char *)&rxcmd[strlen(LORA_AT_CLPM) + 1], NULL, 0);
            if (mode) {
                if ((LowPower_GetState() & e_LOW_POWER_UART)) {
                    LowPower_Enable(e_LOW_POWER_UART);
                }
            } else {
                if(!(LowPower_GetState() & e_LOW_POWER_UART)) {
                    LowPower_Disable(e_LOW_POWER_UART);
                }
            }
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_CSAVE, strlen(LORA_AT_CREPEATERFILTER)) == 0) {
        ret = false;
    } else if (strncmp((const char *)rxcmd, LORA_AT_CGMI, strlen(LORA_AT_CGMI)) == 0) {
        ret = true;
        snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=%s\r\nOK\r\n", LORA_AT_CGMI, aos_mft_itf.get_mft_id());
    } else if (strncmp((const char *)rxcmd, LORA_AT_CGMM, strlen(LORA_AT_CGMM)) == 0) {
        ret = true;
        snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=%s\r\nOK\r\n", LORA_AT_CGMM, aos_mft_itf.get_mft_model());
    } else if (strncmp((const char *)rxcmd, LORA_AT_CGMR, strlen(LORA_AT_CGMR)) == 0) {
        ret = true;
        snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=%s\r\nOK\r\n", LORA_AT_CGMR, aos_mft_itf.get_mft_rev());
    } else if (strncmp((const char *)rxcmd, LORA_AT_CGSN, strlen(LORA_AT_CGSN)) == 0) {
        ret = true;
        snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s=%s\r\nOK\r\n", LORA_AT_CGSN, aos_mft_itf.get_mft_sn());
    } else if (strncmp((const char *)rxcmd, LORA_AT_CGBR, strlen(LORA_AT_CGBR)) == 0) {
        uint32_t baud;
        uint8_t at_cmd_len;
        at_cmd_len = strlen(LORA_AT_CGBR);
        if (rxcmd_index == (at_cmd_len + 2) &&
            strcmp((const char *)&rxcmd[at_cmd_len], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"value\"\r\nOK\r\n", LORA_AT_CGBR);
        } else if (rxcmd_index == (at_cmd_len + 1) &&
                       rxcmd[at_cmd_len] == '?') {
            baud = aos_mft_itf.get_mft_baud();
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%u\r\nOK\r\n", LORA_AT_CGBR, (unsigned int)baud);
        } else if (rxcmd_index > (at_cmd_len + 1) &&
                       rxcmd[at_cmd_len] == '=') {
            baud = strtol((const char *)&rxcmd[at_cmd_len + 1], NULL, 0);   
            
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
            linkwan_serial_output(atcmd, strlen((const char *)atcmd));
            atcmd_index = 0;
            memset(atcmd, 0xff, ATCMD_SIZE);
            aos_mft_itf.set_mft_baud(baud);
#ifdef AOS_KV
            aos_kv_set("sys_baud", &baud, sizeof(baud), true);
#endif             
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_ILOGLVL, strlen(LORA_AT_ILOGLVL)) == 0) {
        if (rxcmd_index == (strlen(LORA_AT_ILOGLVL) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_ILOGLVL)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"level\"\r\nOK\r\n", LORA_AT_ILOGLVL);
        } else if (rxcmd_index == (strlen(LORA_AT_ILOGLVL) + 1) &&
                   rxcmd[strlen(LORA_AT_ILOGLVL)] == '?') {
            int8_t ll = DBG_LogLevelGet();
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:%d\r\nOK\r\n", LORA_AT_ILOGLVL, ll);
        } else if (rxcmd_index == (strlen(LORA_AT_ILOGLVL) + 2) &&
                   rxcmd[strlen(LORA_AT_ILOGLVL)] == '=') {
            int8_t ll = strtol((const char *)&rxcmd[strlen(LORA_AT_ILOGLVL) + 1], NULL, 0);
            DBG_LogLevelSet(ll);
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_IREBOOT, strlen(LORA_AT_IREBOOT)) == 0) {
	    if (rxcmd_index == (strlen(LORA_AT_IREBOOT) + 2) &&
            strcmp((const char *)&rxcmd[strlen(LORA_AT_IREBOOT)], "=?") == 0) {
            snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s:\"Mode\"\r\nOK\r\n", LORA_AT_IREBOOT);
        } else if (rxcmd_index == (strlen(LORA_AT_IREBOOT) + 2) &&
                   rxcmd[strlen(LORA_AT_IREBOOT)] == '=') {
            ret = false;
            int8_t mode = strtol((const char *)&rxcmd[strlen(LORA_AT_IREBOOT) + 1], NULL, 0);
            if (mode == 0 || mode == 1 || mode == 7) {
                snprintf((char *)atcmd, ATCMD_SIZE, "\r\nOK\r\n");
                PRINTF_AT("%s", atcmd);
                aos_lrwan_time_itf.delay_ms(1);
                atcmd_index = 0;
                memset(atcmd, 0xff, ATCMD_SIZE);
                lwan_sys_reboot(mode);
            } else {
                ret = false;
            }
        } else {
            ret = false;
        }
    } else if (strncmp((const char *)rxcmd, LORA_AT_IDEFAULT, strlen(LORA_AT_IDEFAULT)) == 0) {
        ret = false;
    } else if (linkwan_at_custom_handler){
        ret = linkwan_at_custom_handler(rxcmd, rxcmd_index);
        if(ret) {
            goto exit;
        }
    } else {
        ret = false;
    }

    if (ret == false) {
        snprintf((char *)atcmd, ATCMD_SIZE, "\r\n%s%x\r\n", AT_ERROR, 1);
    }

    linkwan_serial_output(atcmd, strlen((const char *)atcmd));
    
exit:    
    atcmd_index = 0;
    memset(atcmd, 0xff, ATCMD_SIZE);
    g_atcmd_processing = false;
}

void linkwan_at_init(void)
{
    atcmd_index = 0;
    memset(atcmd, 0xff, ATCMD_SIZE);
}

void linkwan_at_custom_handler_set(linkwan_at_custom_handler_t handler)
{
    linkwan_at_custom_handler = handler;
}
