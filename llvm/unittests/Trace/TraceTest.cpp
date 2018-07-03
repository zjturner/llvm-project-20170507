#include "llvm/Trace/Trace.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include "llvm/Testing/Support/Error.h"
#include "llvm/Trace/TraceEvent.h"

#include "gtest/gtest.h"

using namespace llvm;
using namespace sys;
using namespace trace;

static uint32_t DummySymbol = 0;
// From TestMain.cpp.
extern const char *TestMainArgv0;

static cl::opt<bool> DoTraceTest("do-trace-test", cl::init(false));

static void printTraceEvent(const TraceEvent &Event) {
  if (const auto *E = dyn_cast<TraceeConnectedEvent>(&Event)) {
    StringRef Image = E->imageFileName();
    Image = llvm::sys::path::filename(Image);
    std::cout << formatv("TraceeConnected[{0}, {1}] ({2}) entry point={3:X}, "
                         "image base={4:X}, thread local area={5:X}\n",
                         Event.pid(), Event.tid(), Image, E->entryPoint(),
                         E->imageBase(), E->threadLocalArea())
                     .str();
  } else if (const auto *E = dyn_cast<TraceeExitedEvent>(&Event)) {
    std::cout << formatv("TraceeExited[{0}, {1}] exit code={2}\n", Event.pid(),
                         Event.tid(), E->exitCode())
                     .str();
  } else if (const auto *E = dyn_cast<LoadLibraryEvent>(&Event)) {
    StringRef Image = E->imageFileName();
    Image = llvm::sys::path::filename(Image);
    std::cout << formatv("LoadLibrary[{0}, {1}] ({2}) load addr={3:X}\n",
                         Event.pid(), Event.tid(), Image, E->loadAddress())
                     .str();
  } else if (const auto *E = dyn_cast<UnloadLibraryEvent>(&Event)) {
    std::cout << formatv("UnloadLibrary[{0}, {1}] load addr={2:X}\n",
                         Event.pid(), Event.tid(), E->loadAddress())
                     .str();
  } else if (const auto *E = dyn_cast<CreateThreadTraceEvent>(&Event)) {
    std::cout << formatv("CreateThread[{0}, {1}] thread start address={2:X}, "
                         "thread local area={3:X}\n",
                         Event.pid(), Event.tid(), E->threadStartAddress(),
                         E->threadLocalArea())
                     .str();
  } else if (const auto *E = dyn_cast<ExitThreadTraceEvent>(&Event)) {
    std::cout << formatv("ExitThread[{0}, {1}] exit code={2}\n", Event.pid(),
                         Event.tid(), E->exitCode())
                     .str();
  } else if (const auto *E =
                 dyn_cast<InstructionBreakpointTraceEvent>(&Event)) {
    std::cout << formatv("InstructionBreakpoint[{0}, {1}] address={2:X}, first "
                         "chance={3}\n",
                         Event.pid(), Event.tid(), E->address(),
                         E->firstChance())
                     .str();
  } else {
    std::cout << formatv("Unknown[{0}, {1}]\n", (unsigned)Event.type(),
                         Event.pid(), Event.tid())
                     .str();
  }
}

TEST(TraceTest, SimpleTrace) {
  if (DoTraceTest)
    return;

  std::string Exe = sys::fs::getMainExecutable(TestMainArgv0, &DummySymbol);

  StringRef ArgV[] = {Exe, "-do-trace-test"};

  Expected<ScopedProcess> Proc = traceLaunch(Exe, ArgV);

  ASSERT_THAT_EXPECTED(Proc, Succeeded());

  bool Exit = false;
  while (!Exit) {
    auto ExpectedEvent = waitForTraceEvent(*Proc);
    ASSERT_THAT_EXPECTED(ExpectedEvent, Succeeded());
    auto Event = std::move(*ExpectedEvent);
    printTraceEvent(*Event);
    Exit = llvm::isa<TraceeExitedEvent>(*Event);
    Error Result = continueFromTraceEvent(std::move(Event));
    ASSERT_THAT_ERROR(std::move(Result), Succeeded());
  }
}
