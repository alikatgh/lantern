// path_sandbox.hpp — resolve game-relative paths under a root, refuse escape.
// Used by every load_* native (wick + Lua) and by package-mode screenshot.
#pragma once
#include <string>

namespace lt {

// True when `rel` is a safe relative path: no empty, no absolute, no "..",
// no leading '.', no backslash. Multi-segment names like "assets/x.bmp" ok.
bool pathIsSafeRel(const std::string& rel);

// Join root + rel. On success, *out is an absolute-or-root-joined path that
// does not escape root (lexical check; also realpath when the file exists).
// On failure returns false and sets *err.
bool pathResolveUnder(const std::string& root, const std::string& rel,
                      std::string& out, std::string& err);

// True when the process is running a game extracted from a .lant package.
void pathSetPackageMode(bool on);
bool pathIsPackageMode();

// Directory under which package-mode screenshots may be written (host temp).
void pathSetScreenshotDir(const std::string& dir);
const std::string& pathScreenshotDir();

// Resolve a screenshot destination: in package mode, force under the
// screenshot dir (basename only of the requested name). In folder mode,
// still refuse absolute / ".." paths — write under the game dir only.
bool pathResolveScreenshot(const std::string& gameDir, const std::string& req,
                           std::string& out, std::string& err);

} // namespace lt
