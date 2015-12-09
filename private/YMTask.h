//
//  YMTask.h
//  yammer
//
//  Created by david on 12/8/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#ifndef YMTask_h
#define YMTask_h

#include "YMBase.h"

#include "YMArray.h"

YM_EXTERN_C_PUSH

typedef const struct __ym_task_t *YMTaskRef;

YMTaskRef YMAPI YMTaskCreate(YMStringRef path, YMArrayRef args, bool saveOutput);

bool YMAPI YMTaskLaunch(YMTaskRef task);
void YMAPI YMTaskWait(YMTaskRef task);
int YMAPI YMTaskGetExitStatus(YMTaskRef task);
unsigned YMAPI char *YMTaskGetOutput(YMTaskRef task, uint32_t *outLength);

YM_EXTERN_C_POP

#endif /* YMTask_h */
