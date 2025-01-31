#pragma once

#include <inttypes.h>

void keystore_init(void);
void keystore_read(void);
char *keystore_error(int rc);
int keystore_deploy(void);
void keystore_reset(void);

enum {
    KEYSTORE_DEPLOY_NOFILE = 1,
    KEYSTORE_DEPLOY_OPEN,
    KEYSTORE_DEPLOY_READ,
};

extern uint8_t ps2_civ[8];
extern int ps2_magicgate;
