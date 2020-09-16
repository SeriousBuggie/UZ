#pragma once

extern "C" {

//  these two values are stored together
//  to improve processor cache hits

typedef struct {
    unsigned prefix, offset;
	operator int() const { return offset; }
} KeyPrefix;

typedef struct {
	//  offset/key prefix
	//  for qsort to use

	KeyPrefix *Keys;
	unsigned *Rank;

	//  During the first round which qsorts the prefix into
	//  order, a groups of equal keys are chained together
	//  into work units for the next round, using
	//  the first two keys of the group

	unsigned WorkChain;
} ThreadData;

KeyPrefix* bwtsort (ThreadData* td, unsigned char *buff, unsigned size);

};