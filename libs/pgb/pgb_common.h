#ifdef PGB_VERSION
    #pragma push_macro("PGB_VERSION")
    #undef PGB_VERSION
#endif

// header guard
#ifndef _PGB_COMMON_H
#define _PGB_COMMON_H

#define PGB_VERSIONED__(X, Y) X##_v##Y
#define PGB_VERSIONED_(X, Y) PGB_VERSIONED__(X, Y)
#define PGB_VERSIONED(X) PGB_VERSIONED_(X, PGB_VERSION)

// memcpy fields in closed range [field_first, field_end] from src to dst->
#define set_fields(dst, src, field_first, field_end) \
    do { \
        memcpy(&dst->field_first, &src->field_first, (uintptr_t)(void*)&src->field_end - (uintptr_t)(void*)&src->field_first + sizeof(src->field_end)); \
        CB_ASSERT(sizeof(src->field_end) == sizeof(dst->field_end)); \
        CB_ASSERT((uintptr_t)(void*)&src->field_end - (uintptr_t)(void*)&src->field_first == (uintptr_t)(void*)&dst->field_end - (uintptr_t)(void*)&dst->field_first); \
    } while(0)
    
#define set_field(dst, src, field) dst->field = src->field

#endif