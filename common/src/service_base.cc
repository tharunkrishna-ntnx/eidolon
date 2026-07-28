#include "eidolon/service_base.h"
#include "eidolon/gflags_util.h"
#include "eidolon/logging.h"

#include <gflags/gflags.h>

#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

DEFINE_string(config_dir, "", "Directory containing .gflags files");
DEFINE_string(eidolon_log_dir, "", "Directory for log output");
DEFINE_string(tmp_dir, "", "Directory for PID files and sockets");

namespace eidolon {

static ServiceBase* g_instance = nullptr;

static void SignalHandler(int sig) {
  if (g_instance) g_instance->Shutdown();
}

ServiceBase::ServiceBase(const std::string& name,
                         const std::string& config_dir,
                         const std::string& log_dir,
                         const std::string& tmp_dir)
    : name_(name),
      config_dir_(config_dir),
      log_dir_(log_dir),
      tmp_dir_(tmp_dir) {}

ServiceBase::~ServiceBase() {
  if (g_instance == this) g_instance = nullptr;
}

void ServiceBase::Init(int argc, char* argv[]) {
  gflags::SetUsageMessage(name_ + " - Eidolon service");
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (!FLAGS_config_dir.empty()) config_dir_ = FLAGS_config_dir;
  if (!FLAGS_eidolon_log_dir.empty()) log_dir_ = FLAGS_eidolon_log_dir;
  if (!FLAGS_tmp_dir.empty()) tmp_dir_ = FLAGS_tmp_dir;

  // Load service-specific gflags file.
  std::string gflags_path =
      gflags_util::GflagsPath(config_dir_, name_);
  if (std::filesystem::exists(gflags_path)) {
    gflags_util::LoadFromFile(gflags_path);
  }

  logging::Init(name_, log_dir_);
  InstallSignalHandlers();
}

void ServiceBase::Run() {
  running_.store(true);
  WritePidFile();
  LOG(INFO) << name_ << " starting (pid=" << getpid() << ")";

  OnStart();

  while (running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  LOG(INFO) << name_ << " shutting down";
  OnStop();
  RemovePidFile();
  logging::Shutdown();
}

void ServiceBase::Shutdown() {
  running_.store(false);
}

void ServiceBase::WritePidFile() {
  namespace fs = std::filesystem;
  fs::create_directories(tmp_dir_);
  std::string path = tmp_dir_ + "/" + name_ + ".pid";
  std::ofstream ofs(path, std::ios::trunc);
  ofs << getpid() << "\n";
}

void ServiceBase::RemovePidFile() {
  std::string path = tmp_dir_ + "/" + name_ + ".pid";
  std::filesystem::remove(path);
}

void ServiceBase::InstallSignalHandlers() {
  g_instance = this;
  struct sigaction sa{};
  sa.sa_handler = SignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
}

}  // namespace eidolon
