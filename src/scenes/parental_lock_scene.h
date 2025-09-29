#pragma once

#include "../scene.h"
#include "pd_api.h"

typedef struct CB_ParentalLockScene
{
    CB_Scene* scene;
    
    bool dismiss : 1;
    
    bool unlocking : 1;
    
    unsigned sel;
    
    unsigned lock_value[4];
    
    unsigned lock_compare[4];
    
} CB_ParentalLockScene;

void check_for_parental_lock(void);
CB_ParentalLockScene* CB_ParentalLockScene_new(void);