//===--- TUScheduler.h -------------------------------------------*-C++-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANGD_TUSCHEDULER_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANGD_TUSCHEDULER_H

#include "ClangdUnit.h"
#include "Function.h"
#include "Threading.h"
#include "llvm/ADT/StringMap.h"
#include <future>

namespace clang {
namespace clangd {

/// Returns a number of a default async threads to use for TUScheduler.
/// Returned value is always >= 1 (i.e. will not cause requests to be processed
/// synchronously).
unsigned getDefaultAsyncThreadsCount();

struct InputsAndAST {
  const ParseInputs &Inputs;
  ParsedAST &AST;
};

struct InputsAndPreamble {
  llvm::StringRef Contents;
  const tooling::CompileCommand &Command;
  const PreambleData *Preamble;
};

/// Determines whether diagnostics should be generated for a file snapshot.
enum class WantDiagnostics {
  Yes,  /// Diagnostics must be generated for this snapshot.
  No,   /// Diagnostics must not be generated for this snapshot.
  Auto, /// Diagnostics must be generated for this snapshot or a subsequent one,
        /// within a bounded amount of time.
};

/// Configuration of the AST retention policy. This only covers retention of
/// *idle* ASTs. If queue has operations requiring the AST, they might be
/// kept in memory.
struct ASTRetentionPolicy {
  /// Maximum number of ASTs to be retained in memory when there are no pending
  /// requests for them.
  unsigned MaxRetainedASTs = 3;
};

class ParsingCallbacks {
public:
  virtual ~ParsingCallbacks() = default;

  /// Called on the AST that was built for emitting the preamble. The built AST
  /// contains only AST nodes from the #include directives at the start of the
  /// file. AST node in the current file should be observed on onMainAST call.
  virtual void onPreambleAST(PathRef Path, ASTContext &Ctx,
                             std::shared_ptr<clang::Preprocessor> PP) {}
  /// Called on the AST built for the file itself. Note that preamble AST nodes
  /// are not deserialized and should be processed in the onPreambleAST call
  /// instead.
  /// The \p AST always contains all AST nodes for the main file itself, and
  /// only a portion of the AST nodes deserialized from the preamble. Note that
  /// some nodes from the preamble may have been deserialized and may also be
  /// accessed from the main file AST, e.g. redecls of functions from preamble,
  /// etc. Clients are expected to process only the AST nodes from the main file
  /// in this callback (obtained via ParsedAST::getLocalTopLevelDecls) to obtain
  /// optimal performance.
  virtual void onMainAST(PathRef Path, ParsedAST &AST) {}
};

/// Handles running tasks for ClangdServer and managing the resources (e.g.,
/// preambles and ASTs) for opened files.
/// TUScheduler is not thread-safe, only one thread should be providing updates
/// and scheduling tasks.
/// Callbacks are run on a threadpool and it's appropriate to do slow work in
/// them. Each task has a name, used for tracing (should be UpperCamelCase).
/// FIXME(sammccall): pull out a scheduler options struct.
class TUScheduler {
public:
  TUScheduler(unsigned AsyncThreadsCount, bool StorePreamblesInMemory,
              std::unique_ptr<ParsingCallbacks> ASTCallbacks,
              std::chrono::steady_clock::duration UpdateDebounce,
              ASTRetentionPolicy RetentionPolicy);
  ~TUScheduler();

  /// Returns estimated memory usage for each of the currently open files.
  /// The order of results is unspecified.
  std::vector<std::pair<Path, std::size_t>> getUsedBytesPerFile() const;

  /// Returns a list of files with ASTs currently stored in memory. This method
  /// is not very reliable and is only used for test. E.g., the results will not
  /// contain files that currently run something over their AST.
  std::vector<Path> getFilesWithCachedAST() const;

  /// Schedule an update for \p File. Adds \p File to a list of tracked files if
  /// \p File was not part of it before.
  /// FIXME(ibiryukov): remove the callback from this function.
  void update(PathRef File, ParseInputs Inputs, WantDiagnostics WD,
              llvm::unique_function<void(std::vector<Diag>)> OnUpdated);

  /// Remove \p File from the list of tracked files and schedule removal of its
  /// resources.
  void remove(PathRef File);

  /// Schedule an async task with no dependencies.
  void run(llvm::StringRef Name, llvm::unique_function<void()> Action);

  /// Schedule an async read of the AST. \p Action will be called when AST is
  /// ready. The AST passed to \p Action refers to the version of \p File
  /// tracked at the time of the call, even if new updates are received before
  /// \p Action is executed.
  /// If an error occurs during processing, it is forwarded to the \p Action
  /// callback.
  void runWithAST(llvm::StringRef Name, PathRef File,
                  Callback<InputsAndAST> Action);

  /// Controls whether preamble reads wait for the preamble to be up-to-date.
  enum PreambleConsistency {
    /// The preamble is generated from the current version of the file.
    /// If the content was recently updated, we will wait until we have a
    /// preamble that reflects that update.
    /// This is the slowest option, and may be delayed by other tasks.
    Consistent,
    /// The preamble may be generated from an older version of the file.
    /// Reading from locations in the preamble may cause files to be re-read.
    /// This gives callers two options:
    /// - validate that the preamble is still valid, and only use it if so
    /// - accept that the preamble contents may be outdated, and try to avoid
    ///   reading source code from headers.
    /// This is the fastest option, usually a preamble is available immediately.
    Stale,
  };
  /// Schedule an async read of the preamble.
  /// If there's no preamble yet (because the file was just opened), we'll wait
  /// for it to build. The result may be null if it fails to build or is empty.
  /// If an error occurs, it is forwarded to the \p Action callback.
  void runWithPreamble(llvm::StringRef Name, PathRef File,
                       PreambleConsistency Consistency,
                       Callback<InputsAndPreamble> Action);

  /// Wait until there are no scheduled or running tasks.
  /// Mostly useful for synchronizing tests.
  bool blockUntilIdle(Deadline D) const;

private:
  /// This class stores per-file data in the Files map.
  struct FileData;

public:
  /// Responsible for retaining and rebuilding idle ASTs. An implementation is
  /// an LRU cache.
  class ASTCache;

  // The file being built/processed in the current thread. This is a hack in
  // order to get the file name into the index implementations. Do not depend on
  // this inside clangd.
  // FIXME: remove this when there is proper index support via build system
  // integration.
  static llvm::Optional<llvm::StringRef> getFileBeingProcessedInContext();

private:
  const bool StorePreamblesInMemory;
  const std::shared_ptr<PCHContainerOperations> PCHOps;
  std::unique_ptr<ParsingCallbacks> Callbacks; // not nullptr
  Semaphore Barrier;
  llvm::StringMap<std::unique_ptr<FileData>> Files;
  std::unique_ptr<ASTCache> IdleASTs;
  // None when running tasks synchronously and non-None when running tasks
  // asynchronously.
  llvm::Optional<AsyncTaskRunner> PreambleTasks;
  llvm::Optional<AsyncTaskRunner> WorkerThreads;
  std::chrono::steady_clock::duration UpdateDebounce;
};

/// Runs \p Action asynchronously with a new std::thread. The context will be
/// propagated.
template <typename T>
std::future<T> runAsync(llvm::unique_function<T()> Action) {
  return std::async(std::launch::async,
                    [](llvm::unique_function<T()> &&Action, Context Ctx) {
                      WithContext WithCtx(std::move(Ctx));
                      return Action();
                    },
                    std::move(Action), Context::current().clone());
}

} // namespace clangd
} // namespace clang

#endif
