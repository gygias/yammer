//
//  AppDelegate.m
//  ProfilingTestApp
//
//  Created by david on 3/20/16.
//  Copyright © 2016 combobulated. All rights reserved.
//

#import "AppDelegate.h"

#define SingleStreamLength ( 512 * 1024 * 1024 )
#define NoInterface -1

@interface AppDelegate ()

@property (weak) IBOutlet NSWindow *window;
@end

@implementation AppDelegate

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)theApplication {
    return YES;
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // Insert code here to initialize your application
    
    self.state = OffState;
    self.activeLabel.stringValue = @"";
    self.tputLabel.stringValue = @"";
    self.sampleLabel.stringValue = @"";
    self.othersLabel.stringValue = @"";
    self.connectionState = IdleState;
    self.connectionStateLabel.stringValue = @"";
    
    // twiddle server checkbox if 2 instances running
    int count = 0;
    for ( NSRunningApplication *app in [[NSWorkspace sharedWorkspace] runningApplications] ) {
        if ( [app.localizedName isEqualToString:@"ProfilingTestApp"] )
            count++;
    }
    if ( count > 1 )
        self.asServerCheckbox.state = NSOffState;
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    // Insert code here to tear down your application
}

- (IBAction)startStopPressed:(id)sender {
    
    BOOL stateOK = YES;
    RunningState newState = ( self.state == OnState ) ? OffState : OnState;
    
    if ( newState == OnState ) {
        NSLog(@"starting");
        
        __unsafe_unretained typeof(self) weakSelf = self;
        self.session = [[YMSession alloc] initWithType:self.typeField.stringValue name:[[NSProcessInfo processInfo] processName]];
        [self.session setStandardHandlers:^(YMSession *session) {
            weakSelf.connectionState = InitializingState;
        } :^(YMSession *session, YMConnection *connection, YMStream *stream) {
            if ( self.asServerCheckbox.state != NSOnState ) {
                [[NSException exceptionWithName:@"client incoming stream" reason:@"shouldn't happen" userInfo:nil] raise];
            } else {
                [weakSelf _consumeServerIncomingStream:stream];
            }
        } :^(YMSession *session, YMConnection *connection, YMStream *stream) {
            NSLog(@"%@: %@: stream closing: %@",session,connection,stream);
        } :^(YMSession *session) {
            NSLog(@"%@: interrupted",session);
            weakSelf.connectionState = InterruptedState;
        }];
        if ( self.session ) {
            
            if ( self.asServerCheckbox.state == NSOnState ) {
                self.connectionState = AdvertisingState;
                stateOK = [self.session startAdvertisingWithName:self.nameField.stringValue
                                                   acceptHandler:^bool(YMSession *session, YMPeer *connection) {
                                                       return true;
                                                   }
                                               connectionHandler:^(YMSession *session, YMConnection *connection) {
                                                   NSLog(@"connected over %@",connection);
                                                   self.connectionState = ConnectedState;
                                                   [self _incomingConnection:connection];
                                               }];
            } else {
                self.connectionState = SearchingState;
                stateOK = [self.session browsePeersWithHandler:^(YMSession *session, YMPeer *peer) {
                    [self.session resolvePeer:peer withHandler:^(YMSession *session_, YMPeer *peer_, BOOL resolved) {
                        if ( resolved ) {
                            NSLog(@"resolved %@",peer_);
                            [self.session connectToPeer:peer_ connectionHandler:^(YMSession *session__, YMConnection *connection) {
                                NSLog(@"connected over %@",connection);
                                self.connectionState = ConnectedState;
                                [self _outgoingConnection:connection];
                            } failureHandler:^(YMSession *session__, YMPeer *peer__) {
                                NSLog(@"outgoing connection failed: %@",peer__);
                                dispatch_async(dispatch_get_main_queue(), ^{ [self startStopPressed:nil]; });
                            }];
                        } else
                            NSLog(@"resolve failed: %@",peer_);
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
        
        [self.session stop];
        self.session = nil;
        self.connectionState = IdleState;
        
        if ( stateOK )
            self.startStopButton.title = @"start";
    }
    
    if ( stateOK )
        self.state = newState;
}

- (void)_incomingConnection:(YMConnection *)connection {
    NSLog(@"incoming connection: %@",connection);
    self.currentConnection = connection;
}

- (void)_outgoingConnection:(YMConnection *)connection {
    NSLog(@"outgoing connection %@",connection);
    self.currentConnection = connection;
    
    dispatch_async(dispatch_get_global_queue(0, 0), ^{
       
        NSUInteger idx = 0;
        self.lastTputDate = [NSDate date];
        self.bytesSinceLastTput = 0;
        while(self.state == OnState) {
       
            NSUInteger streamIdx = 0;
            YMStream *aStream = [connection newStreamWithName:@"some stream"];
            while ( streamIdx < SingleStreamLength ) {
                uint32_t fourBytes = arc4random();
                NSUInteger remaining = SingleStreamLength - streamIdx;
                uint16_t thisLength = ( remaining < sizeof(fourBytes) ) ? (uint16_t)remaining : sizeof(fourBytes);
                [aStream writeData:[NSData dataWithBytes:&fourBytes length:thisLength]];
                
                self.bytesSinceLastTput += thisLength;
                if ( -[self.lastTputDate timeIntervalSinceNow] > 1 ) {
                    
                    NSString *tputString = [NSString stringWithFormat:@"%0.2f mb/s (%0.1f%%)",(double)self.bytesSinceLastTput / 1024 / 1024,
                                            100 * ((self.currentConnection.sample.doubleValue > 0) ?
                                                   ((double)self.bytesSinceLastTput / self.currentConnection.sample.doubleValue)
                                                   : (double)self.bytesSinceLastTput)];
                    self.tputLabel.stringValue = tputString;
                    
                    self.lastTputDate = [NSDate date];
                    self.bytesSinceLastTput = 0;
                }
                
                //if ( streamIdx % 16384 == 0 ) NSLog(@"c[%zu]: wrote %zu-%u",idx,streamIdx,thisLength);
                streamIdx += thisLength;
            }
            [connection closeStream:aStream];
            
            idx++;
            NSLog(@"client finished writing %zuth stream of length %0.1fmb",idx,(float)SingleStreamLength / 1024 / 1024);
        }
        
    });
}

- (void)_consumeServerIncomingStream:(YMStream *)stream {
    
    NSUInteger idx = 0;
    self.lastTputDate = [NSDate date];
    self.bytesSinceLastTput = 0;
    
    while ( idx < SingleStreamLength ) {
        uint16_t aRead = 16384;
        __unused NSData *data = [stream readDataOfLength:aRead];
        if ( ! data ) {
            NSLog(@"server hit eof mid-stream");
            break;
        }
        
        self.bytesSinceLastTput += [data length];
        if ( -[self.lastTputDate timeIntervalSinceNow] > 1 ) {
            
            NSString *tputString = [NSString stringWithFormat:@"%0.2f mb/s (%0.1f%%)",(double)self.bytesSinceLastTput / 1024 / 1024,
                                    100 * ((self.currentConnection.sample.doubleValue > 0) ?
                                           ((double)self.bytesSinceLastTput / self.currentConnection.sample.doubleValue)
                                           : (double)self.bytesSinceLastTput)];
            self.tputLabel.stringValue = tputString;
            
            self.lastTputDate = [NSDate date];
            self.bytesSinceLastTput = 0;
        }
        
        //NSLog(@"s[*]: read %zu-%u: %zu",idx,aRead,[data length]);
        idx += aRead;
    }
    
    NSLog(@"server finished reading stream with length %0.1fmb",(float)idx / 1024 / 1024);
}

@end

@interface InterfaceTransformer : NSValueTransformer
@end

@implementation InterfaceTransformer

- (nullable id)transformedValue:(nullable id)value
{
    return [NSString stringWithFormat:@"%@ <-> %@",[(YMConnection *)value localInterfaceDescription],[(YMConnection *)value remoteInterfaceDescription]];
}

@end

@interface SpeedTransformer : NSValueTransformer
@end

@implementation SpeedTransformer

- (nullable id)transformedValue:(nullable id)value
{
    return [NSString stringWithFormat:@"%0.2f mb/s",[(YMConnection *)value sample].doubleValue / 1024 / 1024];
}

@end

@interface StateTransformer : NSValueTransformer
@end

@implementation StateTransformer

- (nullable id)transformedValue:(nullable id)value
{
    ConnectionState state = [value integerValue];
    switch(state)
    {
        case IdleState:
            return @"";
        case SearchingState:
            return @"searching";
        case AdvertisingState:
            return @"advertised";
        case InitializingState:
            return @"initializing";
        case ConnectedState:
            return @"connected";
        case InterruptedState:
            return @"interrupted...";
        case FailedState:
            return @"connect failed";
        default: ;
    }
    
    return @"?";
}

@end
