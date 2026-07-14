// wick_host.hpp — running a wick game against the lantern C ABI.
// Two ways in:
//  - runWickHost(): the desktop entry — boots the engine, owns the loop.
//  - wickHostInit/Frame/Shutdown: the embedded entry for platforms that
//    own the loop themselves (the iOS host drives one Frame per display-
//    link tick). The caller boots the engine before Init and polls before
//    each Frame; Frame spans lt_frame_begin..end plus the wick GC.
#pragma once
#include <string>

int runWickHost(const std::string& gameDir);

bool wickHostInit(const std::string& gameDir); // after lt_boot
void wickHostFrame();                          // one whole frame
void wickHostShutdown();                       // before lt_shutdown
