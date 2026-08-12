#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace eidolon {

// Base class for long-running Eidolon C++ services.
// Provides signal handling, daemonisation helpers, a health-check TCP
// endpoint, and a graceful shutdown mechanism.
class ServiceBase {
 public:
  ServiceBase(const std::string& name, const std::string& config_dir,
              const std::string& log_dir, const std::string& tmp_dir);
  virtual ~ServiceBase();

  // Parse command-line flags, load the service's .gflags file, initialise
  // logging.  Call this from main() before Run().
  void Init(int argc, char* argv[]);

  // Blocking entry point.  Calls OnStart(), then loops until shutdown is
  // signalled, then calls OnStop().
  void Run();

  // Request graceful shutdown (called from signal handler or health port).
  void Shutdown();

  bool IsRunning() const { return running_.load(); }

  const std::string& Name() const { return name_; }

 protected:
  // Subclasses implement these.
  virtual void OnStart() = 0;   // start threads, open ports, etc.
  virtual void OnStop() = 0;    // clean up

 private:
  void WritePidFile();
  void RemovePidFile();
  void InstallSignalHandlers();

  std::string name_;
  std::string config_dir_;
  std::string log_dir_;
  std::string tmp_dir_;
  std::atomic<bool> running_{false};
};

}  // namespace eidolon
