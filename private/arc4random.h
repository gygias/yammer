#pragma once



#ifdef __cplusplus
extern "C" {
#endif

unsigned int arc4random();
unsigned int arc4random_uniform(unsigned int upper_bound);

#ifdef __cplusplus
}
#endif