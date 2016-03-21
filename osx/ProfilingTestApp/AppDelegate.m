//
//  AppDelegate.m
//  ProfilingTestApp
//
//  Created by david on 3/20/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#import "AppDelegate.h"

@interface AppDelegate ()

@property (weak) IBOutlet NSWindow *window;
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // Insert code here to initialize your application
    
    self.state = OffState;
    self.activeLabel.stringValue = @"";
    self.tputLabel.stringValue = @"";
    self.sampleLabel.stringValue = @"";
    self.othersLabel.stringValue = @"";
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    // Insert code here to tear down your application
}

- (IBAction)startStopPressed:(id)sender {
    
    BOOL stateOK = YES;
    AppState newState = ( self.state == OnState ) ? OffState : OnState;
    
    if ( newState == OnState ) {
        NSLog(@"starting");
        
        self.session = [[YMSession alloc] initWithType:self.typeField.stringValue name:[[NSProcessInfo processInfo] processName]];
        if ( self.session ) {
            
            if ( self.asServerCheckbox.state == NSOnState ) {
                stateOK = [self.session startAdvertisingWithName:self.nameField.stringValue
                                                   acceptHandler:^bool(YMSession *session, YMPeer *connection) {
                                                       return true;
                                                   }
                                               connectionHandler:^(YMSession *session, YMConnection *connection) {
                                                   [self _incomingConnection:connection];
                                               }];
            } else {
                stateOK = [self.session browsePeersWithHandler:^(YMSession *session, YMPeer *peer) {
                    [self.session connectToPeer:peer connectionHandler:^(YMSession *session_, YMConnection *connection) {
                        [self _outgoingConnection:connection];
                    } failureHandler:^(YMSession *session_, YMPeer *peer_) {
                        NSLog(@"outgoing connection failed: %@",peer_);
                        dispatch_async(dispatch_get_main_queue(), ^{ [self startStopPressed:nil]; });
                    }];
                } disappearanceHandler:^(YMSession *session, YMPeer *peer) {
                    NSLog(@"peer disappeared: %@",peer);
                }];
            }
            
            if ( stateOK )
                self.startStopButton.title = @"stop";
            // various enabled states bound in xib
        } else
            NSBeep();
        
    } else if ( newState == OffState ) {
        NSLog(@"stopping");
        
        // need objc stop methods
        //stateOK = [self.session stopAndShit];
        
        if ( stateOK )
            self.startStopButton.title = @"start";
    }
    
    if ( stateOK )
        self.state = newState;
}

- (void)_incomingConnection:(YMConnection *)connection {
    NSLog(@"incoming connection: %@",connection);
}

- (void)_outgoingConnection:(YMConnection *)connection {
    NSLog(@"outgoing connection %@",connection);
}

@end
