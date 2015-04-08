// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef FPLBASE_UTILITIES_H
#define FPLBASE_UTILITIES_H

#include <string>
#include "mathfu/utilities.h"

namespace fpl {

// General utility functions, used by FPLBase, and that might be of use to
// people using the library:

// Constants for use with LogInfo, LogError, etc.
enum LogCategory {
  kApplication = 0,
  kError,
  kSystem,
  kAudio,
  kVideo,
  kRender,
  kInput,
  kCustom
};

typedef int32_t WorldTime;

// Loads a file and returns its contents via string pointer.
bool LoadFile(const char* filename, std::string* dest);

// Search up the directory tree from binary_dir for target_dir, changing the
// working directory to the target_dir and returning true if it's found,
// false otherwise.
bool ChangeToUpstreamDir(const char* const binary_dir,
                         const char* const target_dir);

// Returns true if 16bpp MipMap generation is supported.
// (Basically always true, except on certain android devices.)
bool MipmapGeneration16bppSupported();

// Basic logging functions.  They will output to the console.
void LogInfo(const char* fmt, ...);
void LogError(const char* fmt, ...);
void LogInfo(int category, const char* fmt, ...);
void LogError(int category, const char* fmt, ...);

// Returns the current time, in ticks.
WorldTime GetTicks();

// Delays (sleeps) for the specified number of ticks.
void Delay(WorldTime time);

}  // namespace fpl

#endif  // FPLBASE_UTILITIES_H