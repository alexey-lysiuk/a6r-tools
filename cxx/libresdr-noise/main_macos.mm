/*
 * Copyright (C) 2026 Alexey Lysiuk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "imgui.h"
#include "imgui_impl_osx.h"
#include "imgui_impl_metal.h"

#include "libresdr_noise_gui.h"

@interface AppDelegate : NSObject <NSApplicationDelegate, MTKViewDelegate>
{
    id<MTLCommandQueue> _commandQueue;
    AppState            _state;
}
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    NSRect frame = NSMakeRect(0, 0, 560, 230);
    NSWindowStyleMask style = NSWindowStyleMaskTitled      |
                              NSWindowStyleMaskClosable    |
                              NSWindowStyleMaskResizable   |
                              NSWindowStyleMaskMiniaturizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                    styleMask:style
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
    window.title    = @"LibreSDR Noise Generator";
    window.minSize  = NSMakeSize(420, 200);

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    MTKView* view = [[MTKView alloc] initWithFrame:frame device:device];
    view.delegate                = self;
    view.preferredFramesPerSecond = 60;

    window.contentView = view;
    [window center];
    [window makeKeyAndOrderFront:nil];

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    (void)io;

    ImGui::StyleColorsDark();

    _commandQueue = [device newCommandQueue];
    ImGui_ImplMetal_Init(device);
    ImGui_ImplOSX_Init(view);

    [NSApp activateIgnoringOtherApps:YES];
}

- (void)drawInMTKView:(MTKView*)view
{
    id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];

    MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;
    if (renderPassDescriptor == nil)
    {
        [commandBuffer commit];
        return;
    }

    ImGui_ImplMetal_NewFrame(renderPassDescriptor);
    ImGui_ImplOSX_NewFrame(view);
    ImGui::NewFrame();

    draw_ui_content(_state);

    ImGui::Render();

    renderPassDescriptor.colorAttachments[0].clearColor =
        MTLClearColorMake(0.12, 0.12, 0.12, 1.0);

    id<MTLRenderCommandEncoder> renderEncoder =
        [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    [renderEncoder pushDebugGroup:@"ImGui rendering"];
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);
    [renderEncoder popDebugGroup];
    [renderEncoder endEncoding];

    [commandBuffer presentDrawable:view.currentDrawable];
    [commandBuffer commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
{
    (void)view;
    (void)size;
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
    stop_transmit(&_state.runtime);

    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    (void)sender;
    return YES;
}

@end

static void setup_app_menu()
{
    NSMenu* menubar = [[NSMenu alloc] init];
    NSMenuItem* app_menu_item = [[NSMenuItem alloc] init];
    [menubar addItem:app_menu_item];
    [NSApp setMainMenu:menubar];

    NSMenu* app_menu = [[NSMenu alloc] init];
    NSString* app_name = [[NSProcessInfo processInfo] processName];
    NSString* quit_title = [@"Quit " stringByAppendingString:app_name];
    NSMenuItem* quit_item = [[NSMenuItem alloc] initWithTitle:quit_title
                                                       action:@selector(terminate:)
                                                keyEquivalent:@"q"];
    [app_menu addItem:quit_item];
    [app_menu_item setSubmenu:app_menu];

    [quit_item release];
    [app_menu release];
    [app_menu_item release];
    [menubar release];
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    @autoreleasepool
    {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        setup_app_menu();
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }

    return 0;
}
