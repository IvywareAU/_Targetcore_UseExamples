// Copyright © 2026 Khrustal & Mann
//              MELBOURNE, VICTORIA, AUSTRALIA, 3000
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.
//
//  DialogOrLogFile - the constants the harness and its own child agree on
//
#pragma once

#include "Msgexception.h"              // P2Pevent, and the P2PMSG_CFG_* names

//
//  The scratch folder, under the output directory
//  NOTES: A subfolder rather than the output directory itself, because this
//         harness writes configuration files and P2Pmsg.cfg in the output
//         directory governs every MSCS executable started from there - the
//         sibling NTServiceEventLog.exe among them. Only the one leg that
//         exercises the SEARCH puts a file beside the executable, and it
//         removes it again.
#define WORK_SUBDIR        L"cfgdemo"

//
//  Argument that re-enters this executable as its own detached child
#define ARG_CHILD          L"-child"

//
//  What the child raises, and what the parent looks for in the child's log
//  NOTES: Shared rather than duplicated: the parent asserts on the exact text
//         the child wrote, and two copies of a literal is how that assertion
//         quietly stops meaning anything.
#define CHILD_MESSAGE      L"raised by a blind child - no console, no stderr"

//
//  Where a child records the policy inputs it resolved
//  NOTES: The child's configuration path with this appended, so each leg gets
//         its own and none of them collides. Written before the event is
//         raised: a blocked child never writes anything afterwards, and this
//         file is then the only account of what it decided.
#define STATE_SUFFIX       L".state"

//
//  How long a child gets before it counts as blocked
//  NOTES: Generous. The child does almost nothing, so a healthy one returns in
//         well under a tenth of this; the margin is for a loaded machine, and
//         it only costs wall clock on the ONE leg that is expected to block.
#define DEFAULT_TIMEOUT_MS 4000u
