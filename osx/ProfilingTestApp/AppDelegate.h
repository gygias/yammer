//
//  AppDelegate.h
//  ProfilingTestApp
//
//  Created by david on 3/20/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#import <Cocoa/Cocoa.h>

#import <Yammer/Yammer.h>

typedef enum AppState {
    OffState = 0,
    OnState = 1
} AppState;

@interface AppDelegate : NSObject <NSApplicationDelegate>

@property YMSession *session;
@property AppState state;


@property IBOutlet NSTextField *typeField;
@property IBOutlet NSTextField *nameField;
@property IBOutlet NSButton *asServerCheckbox;
@property IBOutlet NSButton *startStopButton;

@property IBOutlet NSTextField *activeLabel;
@property IBOutlet NSTextField *tputLabel;
@property IBOutlet NSTextField *sampleLabel;
@property IBOutlet NSTextField *othersLabel;

- (IBAction)startStopPressed:(id)sender;

@end

