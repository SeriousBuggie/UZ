#pragma once

extern "C" {

typedef struct {
    unsigned prefix, offset;
	operator int() const { return offset; }
} KeyPrefix;

KeyPrefix* bwtsort (unsigned char *buff, unsigned size);

}