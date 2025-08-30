#pragma once

#include <stdint.h>

#ifdef TARGET_PLAYDATE

void validate_user_stack(void);
typedef void* (*user_stack_fn)(void*);

// note: assembly implementation only supports up to 2 arguments for now
void* call_with_user_stack_impl(user_stack_fn, void* arg, void* arg2);
#define call_with_user_stack(fn) call_with_user_stack_impl((user_stack_fn)fn, NULL, NULL)
#define call_with_user_stack_1(fn, a) \
    call_with_user_stack_impl((user_stack_fn)fn, (void*)(uintptr_t)(a), NULL)
#define call_with_user_stack_2(fn, a, b) \
    call_with_user_stack_impl((user_stack_fn)fn, (void*)(uintptr_t)(a), (void*)(uintptr_t)(b))

// if in user stack, can invoke fn on original stack
// (don't go further than this though!)
// preserves dtcm region, so this could be a slow operation.
void* call_with_main_stack_impl(user_stack_fn, void* arg, void* arg2);

#define call_with_main_stack(fn) call_with_main_stack_impl((user_stack_fn)fn, NULL, NULL)
#define call_with_main_stack_1(fn, a) \
    call_with_main_stack_impl((user_stack_fn)fn, (void*)(uintptr_t)(a), NULL)
#define call_with_main_stack_2(fn, a, b) \
    call_with_main_stack_impl((user_stack_fn)fn, (void*)(uintptr_t)(a), (void*)(uintptr_t)(b))

void* call_with_main_stack_3_impl(user_stack_fn, void* a, void* b, void* c);
#define call_with_main_stack_3(fn, a, b, c)                                                    \
    call_with_main_stack_3_impl(                                                               \
        (user_stack_fn)fn, (void*)(uintptr_t)(a), (void*)(uintptr_t)(b), (void*)(uintptr_t)(c) \
    )
void* call_with_main_stack_4_impl(user_stack_fn, void* a, void* b, void* c, void* d);
#define call_with_main_stack_4(fn, a, b, c, d)                                                  \
    call_with_main_stack_4_impl(                                                                \
        (user_stack_fn)fn, (void*)(uintptr_t)(a), (void*)(uintptr_t)(b), (void*)(uintptr_t)(c), \
        (void*)(uintptr_t)(d)                                                                   \
    )

#else

#define call_with_user_stack(fn) (fn())
#define call_with_user_stack_1(fn, a) (fn(a))
#define call_with_user_stack_2(fn, a, b) (fn(a, b))

#define call_with_main_stack(fn) (fn())
#define call_with_main_stack_1(fn, a) (fn(a))
#define call_with_main_stack_2(fn, a, b) (fn(a, b))
#define call_with_main_stack_3(fn, a, b, c) (fn(a, b, c))
#define call_with_main_stack_4(fn, a, b, c, d) (fn(a, b, c, d))

#endif

void init_user_stack(void);
