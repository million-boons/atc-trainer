#pragma once
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/Time.h"
#include "stt/SpeechRecognizer.h"
#include "stt/SttCatalog.h"

namespace atc {

enum class SttState { NotLoaded, Loading, Ready, Busy, Failed };
const char* toString(SttState s);

struct SttStatus {
  SttState state = SttState::NotLoaded;
  std::string modelId;    // loaded, loading or failed model
  std::string message;    // why it failed, or why the last request was refused
  std::string modelConfig;  // describe() of the loaded model
  bool locked = false;      // an exercise is running (D-39)
  double loadMs = 0;        // last successful load
};

// Owns the one loaded speech model. All engine work runs on its own thread; calls return
// at once. Callbacks run on that thread, or on the caller's when a request is refused.
// Loading a model always frees the previous one first (4 GB panel), and is refused while
// a transmission is being received or recognised, so a result always belongs to one model.
class SttService {
 public:
  using Factory = std::function<std::unique_ptr<ISpeechRecognizer>(const SttModel&, std::string& error)>;
  struct Callbacks {
    std::function<void(const SttStatus&)> status;
    // modelConfig identifies model and settings: store it with the result.
    std::function<void(std::uint64_t tx, const std::string& modelId, const std::string& modelConfig, const SttResult&,
                       Ns releaseToText)>
        result;
  };
  using Describe = std::function<std::string(const SttModel&)>;
  // freeMb: available memory in MB, or a negative value if unknown.
  // describe: a model that cannot be described is not loaded, so every result has a record.
  SttService(std::vector<SttModel> models, Factory factory, Describe describe, Callbacks cb,
             std::function<long()> freeMb = {});
  ~SttService();
  SttService(const SttService&) = delete;
  SttService& operator=(const SttService&) = delete;

  const std::vector<SttModel>& models() const { return models_; }
  SttStatus status() const;

  // False if refused at once; status().message says why.
  bool load(const std::string& id);
  bool unload();

  // For a whole exercise: no load or unload until unlock. False unless a model is Ready
  // and no transmission is open.
  bool lock();
  void unlock();

  // One transmission at a time. begin is allowed while an earlier one is still being
  // recognised; results come in order. False unless a model is loaded.
  bool begin(std::uint64_t tx);
  void accept(std::vector<std::int16_t> audio);
  void finish(Ns releaseTime);
  void cancel();  // short tap: no result

 private:
  bool refuse(const std::string& why);
  void post(std::function<void()> f);
  void run();
  void setStatus(SttState s, const std::string& id, const std::string& msg, double loadMs = -1);

  std::vector<SttModel> models_;
  Factory factory_;
  Describe describe_;
  Callbacks cb_;
  std::function<long()> freeMb_;
  mutable std::mutex m_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> jobs_;
  SttStatus st_;
  bool txOpen_ = false;  // between begin and finish/cancel
  std::uint64_t tx_ = 0;
  int pending_ = 0;      // finished, result not delivered yet
  bool stop_ = false;
  std::unique_ptr<ISpeechRecognizer> engine_;  // worker thread only
  std::thread thread_;
};

long availableMemoryMb();  // from /proc/meminfo, -1 if unknown

}  // namespace atc
