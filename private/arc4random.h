#pragma once

#include "YMBase.h"

#ifdef __cplusplus
extern "C" {
#endif

YMAPI unsigned int arc4random();
YMAPI unsigned int arc4random_uniform(unsigned int upper_bound);

#ifdef __cplusplus
}
#endif