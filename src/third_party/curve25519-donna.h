#pragma once

#include <stdint.h>

int curve25519_donna(uint8_t* out_public, const uint8_t* secret, const uint8_t* basepoint);
