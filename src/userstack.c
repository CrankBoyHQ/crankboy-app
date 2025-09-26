#ifdef TARGET_PLAYDATE

#include "userstack.h"

#include "pd_api.h"
#include "utility.h"

#define USER_STACK_SIZE 0x4000
#define CANARY_VALUE 0x5AC3FA3B

static char user_stack[USER_STACK_SIZE] __attribute__((aligned(8)));

__section__(".rare") static uint32_t* get_stack_start_canary(void)
{
    return (uint32_t*)(user_stack);
}

__section__(".rare") static uint32_t* get_stack_end_canary(void)
{
    return (uint32_t*)(user_stack + USER_STACK_SIZE - sizeof(uint32_t));
}

__section__(".rare") void validate_user_stack(void)
{
    if (*get_stack_start_canary() != CANARY_VALUE || *get_stack_end_canary() != CANARY_VALUE)
    {
        playdate->system->error("User stack canary corrupted");
    }
}

void* user_stack_exit_sp;

__attribute__((naked)) __section__(".rare") void* call_with_main_stack_impl(
    user_stack_fn fn, void* arg, void* arg2
)
{
    __asm__ volatile(
        "push {lr}\n"
        // r3 <- user stasck base
        // lr <- (user stack base + size - 4)
        "ldr r3, =user_stack\n"
        "ldr lr, =" STRINGIFY(
            USER_STACK_SIZE
        ) "\n"

          "add lr, r3, lr\n"
          "sub lr, lr, #4\n"

          // check that we're already on user stack
          "cmp sp, r3\n"
          "blo shift_invoke_then_pop_pc\n"
          "cmp sp, lr\n"
          "bhi shift_invoke_then_pop_pc\n"

          "on_user_stack:\n"
          // r3 <- sp
          // sp <- user_stack_exit_sp
          "mov r3, sp\n"
          "ldr lr, =user_stack_exit_sp\n"
          "ldr lr, [lr]\n"
          "mov sp, lr\n"

          "push {r3}\n"

          // temporarily store dtcm
          "push {r0, r1, r2}\n"
          "bl dtcm_store\n"
          "mov r3, r0\n"
          "pop {r0, r1, r2}\n"

          //
          "push {r3}\n"
          "bl shift_and_invoke\n"
          "pop {r3}\n"

          // restore dtcm
          "push {r0}\n"
          "mov r0, r3\n"
          "bl dtcm_restore\n"
          "pop {r0}\n"

          "pop {r3}\n"
          "mov sp, r3\n"
          "pop {pc}\n"
    );
}

__attribute__((naked)) __section__(".rare") void* call_with_user_stack_impl(
    user_stack_fn fn, void* arg, void* arg2
)
{
    __asm__ volatile(

        "push {lr}\n"
        // r3 <- user stack base
        // lr <- (user stack base + size - 4)
        "ldr r3, =user_stack\n"
        "ldr lr, =" STRINGIFY(
            USER_STACK_SIZE
        ) "\n"

          "add lr, r3, lr\n"
          "sub lr, lr, #4\n"

          // check that we're not already on user stack
          "cmp sp, r3\n"
          "blo not_on_user_stack\n"
          "cmp sp, lr\n"
          "bls shift_invoke_then_pop_pc\n"

          "not_on_user_stack:\n"

          // user_stack_exit_sp <- sp
          "ldr r3, =user_stack_exit_sp\n"
          "str sp, [r3]\n"

          // swap lr and sp
          // (sp <- user stack base + size - 4)
          "mov r3, lr\n"
          "mov lr, sp\n"
          "mov sp, r3\n"

          // save original sp while invoking fn
          "push {lr}\n"
          "bl shift_and_invoke\n"
          "pop {lr}\n"

          // restore original SP
          "mov sp, lr\n"

          // return
          "pop {pc}\n"

          "shift_and_invoke:\n"
          "push {lr}\n"
          // (fallthrough)
          "shift_invoke_then_pop_pc:\n"
          // r3 <- fn, and shift arguments down
          "mov r3, r0\n"
          "mov r0, r1\n"
          "mov r1, r2\n"
          "blx r3\n"  // call fn(args...)

          "push {r0}\n"
          "bl validate_user_stack\n"

          // return r0
          "pop {r0, pc}\n"
    );
}

__section__(".rare") void init_user_stack(void)
{
    *get_stack_start_canary() = CANARY_VALUE;
    *get_stack_end_canary() = CANARY_VALUE;
}

static void* call_with_main_stack_3_helper(void* ufn, void** args)
{
    void* (*fn)(void*, void*, void*) = ufn;
    return fn(args[0], args[1], args[2]);
}

void* call_with_main_stack_3_impl(user_stack_fn ufn, void* a, void* b, void* c)
{
    void* args[] = {a, b, c};
    return call_with_main_stack_2(call_with_main_stack_3_helper, ufn, &args[0]);
}

static void* call_with_main_stack_4_helper(void* ufn, void** args)
{
    void* (*fn)(void*, void*, void*, void*) = ufn;
    return fn(args[0], args[1], args[2], args[3]);
}

void* call_with_main_stack_4_impl(user_stack_fn ufn, void* a, void* b, void* c, void* d)
{
    void* args[] = {a, b, c, d};
    return call_with_main_stack_2(call_with_main_stack_4_helper, ufn, &args[0]);
}

static void* call_with_main_stack_5_helper(void* ufn, void** args)
{
    void* (*fn)(void*, void*, void*, void*, void*) = ufn;
    return fn(args[0], args[1], args[2], args[3], args[4]);
}

void* call_with_main_stack_5_impl(user_stack_fn ufn, void* a, void* b, void* c, void* d, void* e)
{
    void* args[] = {a, b, c, d, e};
    return call_with_main_stack_2(call_with_main_stack_5_helper, ufn, &args[0]);
}

#else

void init_user_stack(void)
{
}

#endif
