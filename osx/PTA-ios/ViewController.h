//
//  ViewController.h
//  PTA-ios
//
//  Created by david on 4/4/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#import <UIKit/UIKit.h>

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

@interface ViewController : UIViewController

@property RunningState state;
@property YMSession *session;
@property YMConnection *currentConnection;
@property ConnectionState connectionState;
@property NSDate *lastTputDate;
@property NSUInteger bytesSinceLastTput;

@property IBOutlet UITextField *typeField;
@property IBOutlet UITextField *nameField;
@property IBOutlet UISwitch *asServerCheckbox;
@property IBOutlet UIButton *startStopButton;

@property IBOutlet UIActivityIndicatorView *connectionSpinner;
@property IBOutlet UILabel *connectionStateLabel;

@property IBOutlet UILabel *activeLabel;
@property IBOutlet UILabel *tputLabel;
@property IBOutlet UILabel *sampleLabel;
@property IBOutlet UILabel *othersLabel;

- (IBAction)startStopPressed:(id)sender;

@end

