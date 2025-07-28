//
//  array.c
//  CrankBoy
//
//  Created by Matteo D'Ignazio on 23/08/21.
//  Maintained and developed by the CrankBoy dev team.
//

#include "array.h"

#include "utility.h"

CB_Array* array_new(void)
{
    CB_Array* array = cb_malloc(sizeof(CB_Array));
    array->length = 0;
    array->capacity = 0;
    array->items = NULL;
    return array;
}

void array_reserve(CB_Array* array, unsigned int capacity)
{
    if (array->capacity >= capacity)
    {
        return;
    }

    array->items = cb_realloc(array->items, capacity * sizeof(void*));
    if (array->items)
    {
        array->capacity = capacity;
    }
}

void array_push(CB_Array* array, void* item)
{
    if (array->length == array->capacity)
    {
        unsigned int new_capacity = (array->capacity == 0) ? 8 : array->capacity * 2;
        array_reserve(array, new_capacity);
    }

    array->items[array->length] = item;
    array->length++;
}

void array_clear(CB_Array* array)
{
    cb_free(array->items);
    array->items = NULL;
    array->length = 0;
    array->capacity = 0;
}

void array_remove_at(CB_Array* array, unsigned int index)
{
    if (index >= array->length)
    {
        return;
    }

    for (unsigned int i = index; i < array->length - 1; i++)
    {
        array->items[i] = array->items[i + 1];
    }

    array->length--;
}

void array_free(CB_Array* array)
{
    if (array)
    {
        cb_free(array->items);
        cb_free(array);
    }
}
