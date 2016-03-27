//
//  AppDelegate.h
//  ProfilingTestApp
//
//  Created by david on 3/20/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#import <Cocoa/Cocoa.h>

#import <Yammer/Yammer.h>

typedef NS_ENUM(NSInteger,RunningState) {
    OffState = 0,
    OnState = 1
};

typedef NS_ENUM(NSInteger,ConnectionState) { // mayhaps this be a library attribute on YMConnectionRef?
    IdleState = 0,
    SearchingState,
    AdvertisingState,
    InitializingState,
    ConnectedState,
    InterruptedState,
    FailedState
};

@interface AppDelegate : NSObject <NSApplicationDelegate>

@property RunningState state;
@property YMSession *session;
@property YMConnection *currentConnection;
@property ConnectionState connectionState;
@property NSDate *lastTputDate;
@property NSUInteger bytesSinceLastTput;

@property IBOutlet NSTextField *typeField;
@property IBOutlet NSTextField *nameField;
@property IBOutlet NSButton *asServerCheckbox;
@property IBOutlet NSButton *startStopButton;

@property IBOutlet NSProgressIndicator *connectionSpinner;
@property IBOutlet NSTextField *connectionStateLabel;

@property IBOutlet NSTextField *activeLabel;
@property IBOutlet NSTextField *tputLabel;
@property IBOutlet NSTextField *sampleLabel;
@property IBOutlet NSTextField *othersLabel;

- (IBAction)startStopPressed:(id)sender;

@end

