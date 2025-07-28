//
//  array.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 23/08/21.
//  Maintained and developed by the CrankBoy dev team.
//

#ifndef array_h
#define array_h

#include <stdbool.h>
#include <stdio.h>

typedef struct
{
    unsigned int length;
    unsigned int capacity;
    void** items;
} CB_Array;

CB_Array* array_new(void);

void array_reserve(CB_Array* array, unsigned int capacity);

void array_push(CB_Array* array, void* item);
void array_clear(CB_Array* array);
void array_free(CB_Array* array);
void array_remove_at(CB_Array* array, unsigned int index);

#endif /* array_h */
