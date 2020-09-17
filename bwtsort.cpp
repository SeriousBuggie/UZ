#include <stdlib.h>
#include <memory.h>

#include "bwtsort.h"

//  set the offset rankings and create
//  new work units for unsorted groups
//  of equal keys

inline void bwtsetranks (ThreadData* td, unsigned from, unsigned cnt)
{
unsigned idx = 0;

    // all members of a group get the same rank

    while( idx < cnt )
        td->Rank[td->Keys[from+idx++].offset] = from;

    // is this a sortable group?

    if( cnt < 2 )
        return;    // final ranking was set

    // if so, add this group to work chain for next round
    // by using the first two key prefix from the group.

    td->Keys[from].prefix = td->WorkChain;
    td->Keys[from + 1].prefix = cnt;
    td->WorkChain = from;
}

//  set the sort key (prefix) from the ranking of the offsets
//  for rounds after the initial one.

inline void bwtkeygroup (ThreadData* td, unsigned from, unsigned cnt, unsigned offset)
{
unsigned off;

  while( cnt-- ) {
    off = td->Keys[from].offset + offset;
    td->Keys[from++].prefix = td->Rank[off];
  }
}

//  the tri-partite qsort partitioning

//  creates two sets of pivot valued
//  elements from [0:leq] and [heq:size]
//  while partitioning a segment of the Keys

inline void bwtpartition (ThreadData* td, unsigned start, unsigned size)
{
KeyPrefix tmp, pvt, *lo;
unsigned loguy, higuy;
unsigned leq, heq;

  while( size > 7 ) {
    // find median-of-three element to use as a pivot
    // and swap it to the beginning of the array
    // to begin the leq group of pivot equals.

    // the larger-of-three element goes to higuy
    // the smallest-of-three element goes to middle

    lo = td->Keys + start;
    higuy = size - 1;
    leq = loguy = 0;

    //  move larger of lo and hi to tmp,hi

    tmp = lo[higuy];

    if( tmp.prefix < lo->prefix )
        lo[higuy] = *lo, *lo = tmp, tmp = lo[higuy];

    //  move larger of tmp,hi and mid to hi

	{
		unsigned size_1 = size >> 1;
		if( lo[size_1].prefix > tmp.prefix )
			lo[higuy] = lo[size_1], lo[size_1] = tmp;

		//  move larger of mid and lo to pvt,lo
		//  and the smaller into the middle

		pvt = *lo;
		
		if( pvt.prefix < lo[size_1].prefix )
			*lo = lo[size_1], lo[size_1] = pvt, pvt = *lo;
	}

    //  start the high group of equals
    //  with a pivot valued element, or not

    if( pvt.prefix == lo[higuy].prefix )
        heq = higuy;
    else
        heq = size;

    while (true) {
        //  both higuy and loguy are already in position
        //  loguy leaves .le. elements beneath it
        //  and swaps equal to pvt elements to leq

        while( ++loguy < higuy )
          if( pvt.prefix < lo[loguy].prefix )
              break;
          else if( pvt.prefix == lo[loguy].prefix )
           if( ++leq < loguy )
            tmp = lo[loguy], lo[loguy] = lo[leq], lo[leq] = tmp;

        //  higuy leaves .ge. elements above it
        //  and swaps equal to pvt elements to heq

        while( --higuy > loguy )
          if( pvt.prefix > lo[higuy].prefix )
              break;
          else if( pvt.prefix == lo[higuy].prefix )
           if( --heq > higuy )
            tmp = lo[higuy], lo[higuy] = lo[heq], lo[heq] = tmp;

        // quit when they finally meet at the empty middle

        if( higuy <= loguy )
            break;

        // element loguy is .gt. element higuy
        // swap them around (the pivot)

        tmp = lo[higuy];
        lo[higuy] = lo[loguy];
        lo[loguy] = tmp;
    }

    // initialize an empty pivot value group

    higuy = loguy;

    //  swap the group of pivot equals into the middle from
    //  the leq and heq sets. Include original pivot in
    //  the leq set.  higuy will be the lowest pivot
    //  element; loguy will be one past the highest.

    //  the heq set might be empty or completely full.

    if( loguy < heq )
      while( heq < size )
        tmp = lo[loguy], lo[loguy++] = lo[heq], lo[heq++] = tmp;
    else
        loguy = size;  // no high elements, they're all pvt valued

    //  the leq set always has the original pivot, but might
    //  also be completely full of pivot valued elements.

    if( higuy > ++leq )
        while( leq )
          tmp = lo[--higuy], lo[higuy] = lo[--leq], lo[leq] = tmp;
    else
        higuy = 0;    // no low elements, they're all pvt valued

    //  The partitioning around pvt is done.
    //  ranges [0:higuy-1] .lt. pivot and [loguy:size-1] .gt. pivot

    //  set the new group rank of the middle range [higuy:loguy-1]
    //  (the .lt. and .gt. ranges get set during their selection sorts)

    bwtsetranks (td, start + higuy, loguy - higuy);

    //  pick the smaller group to partition first,
    //  then loop with larger group.

    if( higuy < size - loguy ) {
        bwtpartition (td, start, higuy);
        size -= loguy;
        start += loguy;
    } else {
        bwtpartition (td, start + loguy, size - loguy);
        size = higuy;
    }
  }

  //  do a selection sort for small sets by
  //  repeately selecting the smallest key to
  //  start, and pulling any group together
  //  for it at leq

  while( size ) {
    for( leq = loguy = 0; ++loguy < size; ) {
		unsigned start_loguy = start + loguy;
      if( td->Keys[start].prefix > td->Keys[start_loguy].prefix )
        tmp = td->Keys[start], td->Keys[start] = td->Keys[start_loguy], td->Keys[start_loguy] = tmp, leq = 0;
      else if( td->Keys[start].prefix == td->Keys[start_loguy].prefix )
       if( ++leq < loguy ) {
		   unsigned start_leq = start + leq;
        tmp = td->Keys[start_leq], td->Keys[start_leq] = td->Keys[start_loguy], td->Keys[start_loguy] = tmp;
	   }
	}

    //  now set the rank for the group of size >= 1

    bwtsetranks (td, start, ++leq);
    start += leq;
    size -= leq;
   }
}

// the main entry point

KeyPrefix* bwtsort (ThreadData* td, unsigned char *buff, unsigned size)
{
unsigned start, cnt, chain;
unsigned offset = 0, off;
unsigned prefix[1];

  //  the Key and Rank arrays include stopper elements

  td->Keys = (KeyPrefix*)malloc ((size + 1 ) * sizeof(KeyPrefix));
  memset (prefix, 0xff, sizeof(prefix));

  // construct the suffix sorting key for each offset

  for( off = size; off--; ) {
    *prefix >>= 8;
    *prefix |= buff[off] << sizeof(prefix) * 8 - 8;
    td->Keys[off].prefix = *prefix;
    td->Keys[off].offset = off;
  }

  // the ranking of each suffix offset,
  // plus extra ranks for the stopper elements

  td->Rank = (unsigned*) malloc ((size + sizeof(prefix)) * sizeof(unsigned));

  // fill in the extra stopper ranks

  for( off = 0; off < sizeof(prefix); off++ )
    td->Rank[size + off] = size + off;

  // perform the initial qsort based on the key prefix constructed
  // above.  Inialize the work unit chain terminator.

  td->WorkChain = size;
  bwtpartition (td, 0, size);

  // the first pass used prefix keys constructed above,
  // subsequent passes use the offset rankings as keys

  offset = sizeof(prefix); 

  // continue doubling the key offset until there are no
  // undifferentiated suffix groups created during a run

  while( td->WorkChain < size ) {
    chain = td->WorkChain;
    td->WorkChain = size;

    // consume the work units created last round
    // and preparing new work units for next pass
    // (work is created in bwtsetranks)

    do {
      start = chain;
      chain = td->Keys[start].prefix;
      cnt = td->Keys[start + 1].prefix;
      bwtkeygroup (td, start, cnt, offset);
      bwtpartition (td, start, cnt);
    } while( chain < size );

    //  each pass doubles the range of suffix considered,
    //  achieving Order(n * log(n)) comparisons

    offset <<= 1;
  }

  //  return the rank of offset zero in the first key

  td->Keys->prefix = td->Rank[0];
  free (td->Rank);
  return td->Keys;
}

#ifdef SORTSTANDALONE
#include <stdio.h>

int main (int argc, char **argv)
{
unsigned size, nxt;
unsigned char *buff;
KeyPrefix *keys;
FILE *in;

    in = fopen(argv[1], "rb");

    fseek(in, 0, 2);
    size = ftell(in);
    fseek (in, 0, 0);
    buff = malloc (size);

    for( nxt = 0; nxt < size; nxt++ )
        buff[nxt] = getc(in);

    keys = bwtsort (buff, size);

    for( nxt = 0; nxt < size; nxt++ )
        putc(buff[keys[nxt].offset], stdout);
}
#endif
