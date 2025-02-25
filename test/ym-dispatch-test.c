//
//  main.c
//  yammer
//
//  Created by david on 12/9/15.
//  Copyright © 2015 combobulated. All rights reserved.
//

#include "YMLog.h"
#include "YMDispatch.h"
#include "YMLock.h"
#include "YMSemaphore.h"
#include "YMUtilities.h"
#include "YMThreadPriv.h"

#if !defined(YMAPPLE)
#include "arc4random.h"
#endif
#include <sys/time.h>
#include <string.h>

#ifndef YMWIN32
# define myexit _Exit
#else
# define myexit exit
#endif

YM_ENTRY_POINT(_ym_dispatch_rep_test);
YM_ENTRY_POINT(denial_of_service_test);
YM_ENTRY_POINT(after_test);
YM_ENTRY_POINT(sources_test);

typedef enum {
    MainTest = 1,
    GlobalTest = MainTest << 1,
    UserTest = GlobalTest << 1,
    RepTests = MainTest | GlobalTest | UserTest,
    DenialOfServiceTest = UserTest << 1,
    AfterTest = DenialOfServiceTest << 1,
    SourcesTest = AfterTest << 1,
    ArglessTests = DenialOfServiceTest | AfterTest | SourcesTest
} TestType;

void usage(void) { ymlog("usage: ym-dispatch-test [m|g[r]|u|dos|after|sources] [#nReps]"); exit(1); }

static int gNumberArg2 = 0, gIter = 0, gCompleted = 0;
static bool gRacey = false;
static TestType gType;
static YMLockRef gLock = NULL;
static YMDispatchQueueRef gUserQueue;
static bool gEneralStateOfAccumulatedHappiness = true;

static YMDictionaryRef gRepsByThread = NULL;
static YMLockRef gRepsByThreadLock = NULL;

int main(int argc, char *argv[])
{
    if ( argc < 2 || argc > 3 )
        usage();
    if ( strcmp(argv[1],"m") == 0 )
        gType = MainTest;
    else if ( strcmp(argv[1],"g") == 0 || ( gRacey = ( strcmp(argv[1],"gr") == 0 ) ) )
        gType = GlobalTest;
    else if ( strcmp(argv[1],"u") == 0 )
        gType = UserTest;
    else if ( strcmp(argv[1],"dos") == 0 )
        gType = DenialOfServiceTest;
    else if ( strcmp(argv[1],"after") == 0 ) 
        gType = AfterTest;
    else if ( strcmp(argv[1],"sources") == 0 )
        gType = SourcesTest;
    else
        usage();
    
    gNumberArg2 = argc == 3 ? atoi(argv[2]) : 0;

    if ( gNumberArg2 <= 1 && ! ( gType & ArglessTests ) )
        usage();
        
    
    if ( gType & RepTests ) {
        ymlog("ym-dispatch-test targeting %s with %d %sreps",(gType==MainTest)?"main":(gType==GlobalTest)?"global":"user",gNumberArg2,gRacey?"*RACEY* ":"");

        gRepsByThread = YMDictionaryCreate();
        gRepsByThreadLock = YMLockCreate();
        ym_dispatch_user_t dispatch = { _ym_dispatch_rep_test, NULL, false, ym_dispatch_user_context_noop };
        if ( gType == MainTest ) {
            YMDispatchAsync(YMDispatchGetGlobalQueue(),&dispatch);
        } else if ( gType == GlobalTest ) {
            if ( ! gRacey )
                gLock = YMLockCreate();
            YMDispatchAsync(YMDispatchGetMainQueue(), &dispatch);
        } else {
            gUserQueue = YMDispatchQueueCreate(YMSTRC("ym-dispatch-test-queue"));
            YMDispatchAsync(YMDispatchGetGlobalQueue(), &dispatch);
        }
    } else if ( gType == DenialOfServiceTest ) {
        ymlog("ym-dispatch-test running denial-of-service test");

        ym_dispatch_user_t dispatch = { denial_of_service_test, NULL, false, ym_dispatch_user_context_noop };
        YMDispatchAsync(YMDispatchGetMainQueue(), &dispatch);
    } else if ( gType == AfterTest ) {
        ymlog("ym-dispatch-test: \"the after show is about to begin!\"");

        ym_dispatch_user_t dispatch = { after_test, NULL, false, ym_dispatch_user_context_noop };
        YMDispatchAsync(YMDispatchGetMainQueue(), &dispatch);
    } else if ( gType == SourcesTest ) {
        ymlog("ym-dispatch-test running sources test");

        ym_dispatch_user_t dispatch = { sources_test, NULL, false, ym_dispatch_user_context_noop };
        YMDispatchAsync(YMDispatchGetMainQueue(), &dispatch);
    }
    
    YMDispatchMain();

    ymassert(false,"main() regained control");
    return 1;
}

#pragma mark "rep" based tests

void print_reps_by_thread(void)
{
    uint32_t reps = 0;
    YMDictionaryEnumRef dEnum = YMDictionaryEnumeratorBegin(gRepsByThread);
    while ( dEnum ) {
        fprintf(stdout,"thread %08lx %u reps (%0.2f%%)\n",(unsigned long)dEnum->key,*((uint32_t *)dEnum->value),(double)*((uint32_t *)dEnum->value)/(double)gNumberArg2 * 100);
        reps += *((uint32_t *)dEnum->value);
        dEnum = YMDictionaryEnumeratorGetNext(dEnum);
    }
    fprintf(stdout,"===========================================\n\t\t%u reps\n",reps);
    fflush(stdout);
}

YM_ENTRY_POINT(_do_a_work)
{
    YMLockLock(gRepsByThreadLock);
    unsigned long tn = _YMThreadGetCurrentThreadNumber();
    if ( ! YMDictionaryContains(gRepsByThread,(const void *)tn) )
        YMDictionaryAdd(gRepsByThread,(const void *)tn,YMALLOC(sizeof(uint32_t)));
    uint32_t *c = (uint32_t *)YMDictionaryGetItem(gRepsByThread,(const void *)tn);
    (*c)++;
    YMLockUnlock(gRepsByThreadLock);

    bool lock = gLock != NULL;
    if ( lock ) YMLockLock(gLock);
    gCompleted++;
    if ( gCompleted % 10000 == 0 ) { printf("%d%s",gCompleted,gCompleted==gNumberArg2?"\n":" "); fflush(stdout); }
    if ( lock ) YMLockUnlock(gLock);
    
    if ( gCompleted == gNumberArg2 ) {
        fprintf(stdout,"ym-dispatch-test completed %d reps\n",gNumberArg2);
        print_reps_by_thread();
        myexit(0);
    }
}

YM_ENTRY_POINT(valgrind_take_me_away)
{
    free(context);
}

void * valgrind_hit_me(void)
{
    size_t relativelyBig = 1024;
    return malloc(relativelyBig);
}

YM_ENTRY_POINT(_ym_dispatch_rep_test)
{
    YMDispatchQueueRef queue = NULL;
    if ( gType == MainTest )
        queue = YMDispatchGetMainQueue();
    else if ( gType == GlobalTest )
        queue = YMDispatchGetGlobalQueue();
    else
        queue = gUserQueue;

    for( gIter = 0; gIter < gNumberArg2; gIter++ ) {
        ym_dispatch_user_t aDispatch = { _do_a_work, NULL, false, ym_dispatch_user_context_noop };
        switch(arc4random_uniform(3)) {
            case 0:
                YMDispatchAsync(queue, &aDispatch);
                break;
            case 1:
                aDispatch.context = valgrind_hit_me();
                aDispatch.onCompleteProc = valgrind_take_me_away;
                YMDispatchAsync(queue, &aDispatch);
                break;
            default:
                aDispatch.context = valgrind_hit_me();
                aDispatch.mode = ym_dispatch_user_context_free;
                YMDispatchAsync(queue, &aDispatch);
                break;
        }
    }

    if ( gType == GlobalTest && gRacey ) {
        // it's now possible to <i>know</i>
        sleep(10); // could perhaps check % cpu
        printf("ym-dispatch-test assumes races have finished at (%d / %d)\n",gCompleted,gNumberArg2);
        print_reps_by_thread();
        myexit(1);
    }
}

YM_ENTRY_POINT(denial_of_service)
{
    ymlog("sorry, basement flooded again");
    sleep(99999999);
}

YM_ENTRY_POINT(denial_of_service_check)
{
    fprintf(stderr,"i will fix your bugs!\n");
    fflush(stderr);
    myexit(0);
}

YM_ENTRY_POINT(denial_of_service_finally)
{
    ymlog("rush limbaugh");
    sleep(5);
    myexit(1);
}

YM_ENTRY_POINT(denial_of_service_test)
{
    int threadsPerGlobalQueue = YMGetDefaultThreadsForCores(YMGetNumberOfCoresAvailable()) * 2;
    int idx = threadsPerGlobalQueue;
    for ( idx = threadsPerGlobalQueue; idx; idx-- ) {
        ym_dispatch_user_t user = { denial_of_service, NULL, NULL, ym_dispatch_user_context_noop };
        YMDispatchAsync(YMDispatchGetGlobalQueue(),&user);
    }

    ym_dispatch_user_t finally = { denial_of_service_finally, NULL, NULL, ym_dispatch_user_context_noop };
    YMDispatchAfter(YMDispatchGetMainQueue(),&finally, 5);

    while (1) {
        ym_dispatch_user_t check = { denial_of_service_check, NULL, NULL, ym_dispatch_user_context_noop };
        YMDispatchAsync(YMDispatchGetGlobalQueue(),&check);
        sleep(1);
    }
}

#pragma mark after test

YM_ENTRY_POINT(after_conversion_test);
YM_ENTRY_POINT(after_tells_a_story);

static YMSemaphoreRef gAfterTestSemaphore = NULL;

YM_ENTRY_POINT(after_test)
{
    unsigned int random;
    uint64_t pid =
    #if !defined(YMWIN32)
            getpid();
    #else
            GetCurrentProcessId();
    #endif
    do { random = arc4random_uniform(arc4random()) % pid; } while ( arc4random_uniform(random) % 1000 != 0 );

    ym_dispatch_user_t conversionUser = { after_conversion_test, NULL, NULL, ym_dispatch_user_context_noop };
    YMDispatchSync(YMDispatchGetGlobalQueue(), &conversionUser);

    ym_dispatch_user_t storyUser = { after_tells_a_story, NULL, NULL, ym_dispatch_user_context_noop };
    YMDispatchSync(YMDispatchGetGlobalQueue(), &storyUser);
}

YM_ENTRY_POINT(after_conversion_test)
{
    int difference = 1;
    struct timeval tvThen, tvNow;
    int ret = gettimeofday(&tvThen, NULL);
    if ( ret != 0 ) {
        ymlog("gettimeofday failed: %d %s",errno,strerror(errno));
        gEneralStateOfAccumulatedHappiness &= false;
        return;
    }

    struct timespec tsThen, tsNow;
    ret = clock_gettime(CLOCK_REALTIME,&tsThen);
    if ( ret != 0 ) {
        printf("clock_gettime failed: %d %s",errno,strerror(errno));
        gEneralStateOfAccumulatedHappiness &= false;
        return;
    }

    sleep(difference);

    ret = gettimeofday(&tvNow, NULL);
    if ( ret != 0 ) {
        ymlog("gettimeofday failed: %d %s",errno,strerror(errno));
        gEneralStateOfAccumulatedHappiness &= false;
        return;
    }
    
    ret = clock_gettime(CLOCK_REALTIME,&tsNow);
    if ( ret != 0 ) {
        printf("clock_gettime failed: %d %s",errno,strerror(errno));
        gEneralStateOfAccumulatedHappiness &= false;
        return;
    }

// it is a scientific fact that apple users have a lower threshold to happiness
#if defined(YMAPPLE)
# define kThresholdofHappiness .01
#else
# define kThresholdofHappiness .001
#endif
    double tvDoubleSince = YMTimevalSince(tvThen, tvNow);
    double tvDelta = tvDoubleSince + difference;
    bool tvHappy = ( tvDelta > -kThresholdofHappiness && tvDelta <= 0 );
    ymlog("%shappy with tvDoubleSince=%0.6f, tvDelta=%0.6f",tvHappy?"":"un",tvDoubleSince,tvDelta);

    double tsDoubleSince = YMTimespecSince(tsThen, tsNow);
    double tsDelta = tsDoubleSince + difference;
    bool tsHappy = ( tsDelta > -kThresholdofHappiness && tsDelta <= 0 );
    ymlog("%shappy with tsDoubleSince=%0.9f, tsDelta=%0.9f",tsHappy?"":"un",tsDoubleSince,tsDelta);

    gEneralStateOfAccumulatedHappiness &= ( tvHappy & tsHappy );

    ComparisonResult result = YMTimevalCompare(tvThen,tvNow);
    ymlog("Thenish is%s less than nowish.",(result == LessThan)?"":" NOT");
    gEneralStateOfAccumulatedHappiness &= result == LessThan;

    result = YMTimevalCompare(tvNow,tvNow);
    ymlog("Nowish is%s nowish.",(result == EqualTo)?"":" NOT");
    gEneralStateOfAccumulatedHappiness &= result == EqualTo;

    result = YMTimevalCompare(tvThen,tvThen);
    ymlog("Thenish is%s thenish.",(result == EqualTo)?"":" NOT");
    gEneralStateOfAccumulatedHappiness &= result == EqualTo;

    result = YMTimevalCompare(tvNow,tvThen);
    ymlog("Nowish is%s greater than thenish (The Power of Nowish℠).",(result == GreaterThan)?"":" NOT");
    gEneralStateOfAccumulatedHappiness &= result == GreaterThan;

    result = YMTimespecCompare(tsThen,tsNow);
    ymlog("Then is%s less than now.",(result == LessThan)?"":" NOT");
    gEneralStateOfAccumulatedHappiness &= result == LessThan;

    result = YMTimespecCompare(tsThen,tsThen);
    ymlog("Then is%s then.",(result == EqualTo)?"":" NOT");
    gEneralStateOfAccumulatedHappiness &= result == EqualTo;

    result = YMTimespecCompare(tsNow,tsNow);
    ymlog("Now is%s now.",(result == EqualTo)?"":" NOT");
    gEneralStateOfAccumulatedHappiness &= result == EqualTo;

    result = YMTimespecCompare(tsNow,tsThen);
    ymlog("Now is%s greater than then (The Power of Now℠).",(result == GreaterThan)?"":" NOT");
    gEneralStateOfAccumulatedHappiness &= result == GreaterThan;


}

static YMDispatchQueueRef gAQueueOfOurOwn = NULL;
const static int gAfterStorySecondString = 10;

typedef struct after_story_context
{
    char name[25];
    struct timeval thenish;
    double aRandom;
    bool ended;
    bool satisfied;
} after_story_context;
typedef struct after_story_context after_story_context_t;

YM_ENTRY_POINT(an_after_story_plot_device)
{
    after_story_context_t *aCharacter = context;
    aCharacter->ended = true;
    YMSemaphoreSignal(gAfterTestSemaphore);

    struct timeval nowish;
    gettimeofday(&nowish, NULL);

    double timeSinceThenish = YMTimevalSince(aCharacter->thenish, nowish);
    double delta = timeSinceThenish + aCharacter->aRandom;

#if defined(YMAPPLE)
# define kThresholdOfCustomerSatisfaction 1.0
#else
# define kThresholdOfCustomerSatisfaction 0.1
#endif
    // we pride ourselves on not taking things too seriously
    aCharacter->satisfied = delta >= -kThresholdOfCustomerSatisfaction && delta <= 0;
    ymlog("After %f seconds of active contemplation, I, %s, am %ssatisfied! (%0.6f)",aCharacter->aRandom,
                                                                                    aCharacter->name,
                                                                                    aCharacter->satisfied?"":"NOT ",
                                                                                    delta);
}

YM_ENTRY_POINT(an_after_story_report)
{
    after_story_context_t *aCharacter = context;
    ymlog("%s reports that they are %s",    aCharacter->name, 
                                            aCharacter->ended ? 
                                            ( aCharacter->satisfied ? "ended, satisfied, and enjoying a cigarette." :
                                                                      "ended, but unsatisfied. 明日があるさ!" ) :
                                            ( aCharacter->satisfied ? "not ended, but satisfied. The ladies man!" :
                                                                      "NOT ended, NOT satisfied, leaving their representative a nasty voicemail, and bragging about it on social media." ));
    gEneralStateOfAccumulatedHappiness &= aCharacter->ended & aCharacter->satisfied;
}

YM_ENTRY_POINT(an_after_story_shock_ending)
{
    bool shockEnding = YMSemaphoreTest(gAfterTestSemaphore,false);
    bool postCreditsScene = YMSemaphoreTest(gAfterTestSemaphore,false);

    ymlog("%s%s", !shockEnding ? "in HELL!" : "The End...", !shockEnding ? " Where did it all go wrong?" : ( ! postCreditsScene ? " (whew)" : " (or is it?)" ));
    sleep(1);

    gEneralStateOfAccumulatedHappiness &= shockEnding;
    myexit(gEneralStateOfAccumulatedHappiness?0:1);
}

YM_ENTRY_POINT(an_after_story_surprise_twist)
{
    int i = gNumberArg2 ? gNumberArg2 : gAfterStorySecondString;
    bool semOkay = true;
    while ( --i ) {
        semOkay &= YMSemaphoreTest(gAfterTestSemaphore,false);
        printf("%d %s\n",i,semOkay?"I'm h...a...p...p...y...":"It hurts, Ness...");
        usleep(50000);
    }

    ymlog("and they all lived happy %sever after...", semOkay ? ( gEneralStateOfAccumulatedHappiness ? "" : "n" ) :
                                                                        "N" );

    gEneralStateOfAccumulatedHappiness &= semOkay;

    ym_dispatch_user_t user = { an_after_story_shock_ending, NULL, NULL, ym_dispatch_user_context_noop };
    YMDispatchAfter(gAQueueOfOurOwn,&user,3.0);
}

YM_ENTRY_POINT(after_tells_a_story)
{
    gAfterTestSemaphore = YMSemaphoreCreate(0);
    YMArrayRef binderFullOfCharacters = YMArrayCreate();
    unsigned int testLengthMax = 10;
    int numberOfCharacters = gNumberArg2 ? gNumberArg2 : gAfterStorySecondString;

    ymlog("places everyone, places! the audience is %s the show",gEneralStateOfAccumulatedHappiness?"anxious for":"already bored of");
    sleep(1);

    for( int i = 0; i < numberOfCharacters; i++ ) {
        unsigned int aRandom;
        while ( 0 == ( aRandom = arc4random_uniform(testLengthMax) ) ) {}
        after_story_context_t *aCharacter = malloc(sizeof(after_story_context_t));
        snprintf(aCharacter->name,sizeof(aCharacter->name),"Character #%d",i+1);
        aCharacter->aRandom = aRandom;
        aCharacter->ended = false;
        aCharacter->satisfied = false;
        struct timeval nowish;
        gettimeofday(&nowish, NULL);
        memcpy(&aCharacter->thenish,&nowish,sizeof(nowish));

        ym_dispatch_user_t user = { an_after_story_plot_device, aCharacter, NULL, ym_dispatch_user_context_noop };
        YMDispatchAfter(YMDispatchGetGlobalQueue(),&user,aCharacter->aRandom);

        YMArrayAdd(binderFullOfCharacters,aCharacter);

        ymlog("i, %s, take the stage in %u seconds!",aCharacter->name,aRandom);
        usleep(100000);
    }

    ymlog("sleeping %u + 1 seconds for After Test One to complete...",testLengthMax);
    sleep(testLengthMax + 1);

    ymlog("let's see...");
    double artificialIntelligenceNaturalDelayModel = .5;
    for ( int i = 0; i < numberOfCharacters; i++ ) {
        after_story_context_t *aCharacter = (after_story_context_t *)YMArrayGet(binderFullOfCharacters,i);
        ym_dispatch_user_t user = { an_after_story_report, aCharacter, NULL, ym_dispatch_user_context_free };
        YMDispatchAfter(YMDispatchGetMainQueue(),&user,(i+1)*artificialIntelligenceNaturalDelayModel);
    }

    gAQueueOfOurOwn = YMDispatchQueueCreate(YMSTRC("A Queue Of Our Own"));
    ym_dispatch_user_t user = { an_after_story_surprise_twist, NULL, NULL, ym_dispatch_user_context_noop };
    YMDispatchAfter(gAQueueOfOurOwn,&user,1 + (1 + numberOfCharacters)*artificialIntelligenceNaturalDelayModel);
}

#pragma mark sources test

YM_ENTRY_POINT(sources_test)
{
    
}
