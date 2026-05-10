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

// ════════════════════════════════════════════════════════════════════════════
// macOS — Metal + OSX (AppKit / MTKView)
// ════════════════════════════════════════════════════════════════════════════

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "imgui.h"
#include "imgui_impl_osx.h"
#include "imgui_impl_metal.h"

#include "tinysa_sweep_time_gui.h"

@interface AppDelegate : NSObject <NSApplicationDelegate, MTKViewDelegate>
{
    id<MTLCommandQueue> _commandQueue;
    AppState            _state;
}
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    // Create window
    NSRect frame = NSMakeRect(0, 0, 760, 420);
    NSWindowStyleMask style = NSWindowStyleMaskTitled      |
                              NSWindowStyleMaskClosable    |
                              NSWindowStyleMaskResizable   |
                              NSWindowStyleMaskMiniaturizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.title    = @"tinySA Ultra Sweep Time Calculator";
    window.minSize  = NSMakeSize(520, 360);

    // Create Metal device and MTKView
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    MTKView* view = [[MTKView alloc] initWithFrame:frame device:device];
    view.delegate                = self;
    view.preferredFramesPerSecond = 60;

    window.contentView = view;
    [window center];
    [window makeKeyAndOrderFront:nil];

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    (void)io;

    ImGui::StyleColorsDark();

    // Initialise backends
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

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    @autoreleasepool
    {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app run];
    }

    return 0;
}
