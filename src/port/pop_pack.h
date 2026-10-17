#ifndef POP_PACK_H
#define POP_PACK_H

#include <stdint.h>
#include <stdbool.h>

#define POP_PACK_MAGIC "GWPOP1\0\0"

enum {
    POP_PACK_OK = 0,
    POP_PACK_ERR_MAGIC = -1,
    POP_PACK_ERR_CORRUPT = -2,
};

int pop_pack_open(const uint8_t *data, uint32_t size);
bool pop_pack_find(const char *name, const uint8_t **data, uint32_t *size);
uint32_t pop_pack_count(void);
const char *pop_pack_name(uint32_t index);

#endif
