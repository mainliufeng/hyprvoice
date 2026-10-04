#include "asr.h"
#include "fun_backend.h"
#include "transcript_text.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glib.h>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
namespace hv {
static std::string Asset(const std::string &dir, const std::string &name) {
  auto p = std::filesystem::path(dir) / name;
  if (!std::filesystem::is_regular_file(p))
    throw std::runtime_error("Model file missing: " + p.string());
  return p.string();
}
Asr::Asr(const Config &c) {
  if (c.data.at("asr").at("backend") == "fun") {
    fun_ = std::make_unique<FunBackend>(c);
    return;
  }
  std::string hotwords;
  if (!c.path("hotwords").empty()) {
    hotwords = ReadFile(c.path("hotwords"));
    if (!g_utf8_validate(hotwords.data(), hotwords.size(), nullptr))
      throw std::runtime_error("Hotwords must be UTF-8");
    std::istringstream lines(hotwords);
    std::string line, clean;
    while (std::getline(lines, line)) {
      auto b = line.find_first_not_of(" \t\r");
      if (b == std::string::npos || line[b] == '#')
        continue;
      auto e = line.find_last_not_of(" \t\r");
      line = line.substr(b, e - b + 1);
      auto colon = line.rfind(':');
      if (colon != std::string::npos && colon > 0 && line[colon - 1] != ' ')
        line.insert(colon, " ");
      clean += line + '\n';
    }
    hotwords = std::move(clean);
  }
  auto dir = c.path("streaming_model");
  auto encoder = Asset(dir, "encoder.int8.onnx"),
       decoder = Asset(dir, "decoder.onnx"),
       joiner = Asset(dir, "joiner.int8.onnx"),
       tokens = Asset(dir, "tokens.txt"), vocab = Asset(dir, "bpe.vocab");
  SherpaOnnxOnlineRecognizerConfig cfg{};
  cfg.feat_config = {16000, 80};
  cfg.model_config.transducer = {encoder.c_str(), decoder.c_str(),
                                 joiner.c_str()};
  cfg.model_config.tokens = tokens.c_str();
  cfg.model_config.num_threads = 1;
  cfg.model_config.provider = "cpu";
  cfg.model_config.modeling_unit = "bpe";
  cfg.model_config.bpe_vocab = vocab.c_str();
  cfg.decoding_method =
      hotwords.empty() ? "greedy_search" : "modified_beam_search";
  cfg.max_active_paths = 4;
  cfg.rule1_min_trailing_silence = 2.4;
  cfg.rule2_min_trailing_silence = 1.2;
  cfg.rule3_min_utterance_length = 20;
  cfg.hotwords_score = 1.5;
  cfg.hotwords_buf = hotwords.c_str();
  cfg.hotwords_buf_size = hotwords.size();
  online_ = SherpaOnnxCreateOnlineRecognizer(&cfg);
  if (!online_)
    throw std::runtime_error("Streaming model initialization failed");
  try {
    dir = c.path("offline_model");
    encoder = Asset(dir, "encoder-epoch-99-avg-1.int8.onnx");
    decoder = Asset(dir, "decoder-epoch-99-avg-1.onnx");
    joiner = Asset(dir, "joiner-epoch-99-avg-1.int8.onnx");
    tokens = Asset(dir, "tokens.txt");
    vocab = Asset(dir, "bpe.vocab");
    SherpaOnnxOfflineRecognizerConfig off{};
    off.feat_config = {16000, 80};
    off.model_config.transducer = {encoder.c_str(), decoder.c_str(),
                                   joiner.c_str()};
    off.model_config.tokens = tokens.c_str();
    off.model_config.num_threads = 1;
    off.model_config.provider = "cpu";
    off.model_config.modeling_unit = "bpe";
    off.model_config.bpe_vocab = vocab.c_str();
    off.decoding_method = "modified_beam_search";
    off.max_active_paths = 4;
    off.hotwords_score = 1.5;
    std::string hotFile;
    if (!hotwords.empty()) {
      auto tmp =
          std::filesystem::temp_directory_path() / "hyprvoice-hotwords-XXXXXX";
      std::string pattern = tmp.string();
      int fd = mkstemp(pattern.data());
      if (fd < 0)
        throw std::runtime_error("Cannot create temporary hotwords file");
      close(fd);
      hotFile = pattern;
      std::ofstream(hotFile) << hotwords;
      off.hotwords_file = hotFile.c_str();
    }
    offline_ = SherpaOnnxCreateOfflineRecognizer(&off);
    if (!hotFile.empty())
      std::filesystem::remove(hotFile);
    if (!offline_)
      throw std::runtime_error("Offline model initialization failed");
    Asset(std::filesystem::path(c.path("vad_model")).parent_path().string(),
          std::filesystem::path(c.path("vad_model")).filename().string());
    speech_ = std::make_unique<SpeechDetector>(c.path("vad_model"));
  } catch (...) {
    if (offline_)
      SherpaOnnxDestroyOfflineRecognizer(offline_);
    SherpaOnnxDestroyOnlineRecognizer(online_);
    throw;
  }
}
Asr::~Asr() {
  cancel();
  if (online_)
    SherpaOnnxDestroyOnlineRecognizer(online_);
  if (offline_)
    SherpaOnnxDestroyOfflineRecognizer(offline_);
}
void Asr::begin(const std::atomic<bool> *cancelled) {
  cancel();
  if (fun_) {
    fun_->prepare(cancelled);
    recording_ = true;
    return;
  }
  stream_ = SherpaOnnxCreateOnlineStream(online_);
  if (!stream_)
    throw std::runtime_error("Cannot create recognition stream");
}
std::string Asr::current() {
  auto r = SherpaOnnxGetOnlineStreamResult(online_, stream_);
  std::string text = r && r->text ? r->text : "";
  if (r)
    SherpaOnnxDestroyOnlineRecognizerResult(r);
  return text;
}
std::string Asr::push(std::span<const float> samples) {
  if (!stream_ && !recording_)
    throw std::runtime_error("No recognition session");
  samples_.insert(samples_.end(), samples.begin(), samples.end());
  if (fun_)
    return {};
  if (!samples.empty())
    SherpaOnnxOnlineStreamAcceptWaveform(stream_, 16000, samples.data(),
                                         samples.size());
  while (SherpaOnnxIsOnlineStreamReady(online_, stream_))
    SherpaOnnxDecodeOnlineStream(online_, stream_);
  return current();
}
std::string Asr::decodeOffline(const std::vector<float> &samples) {
  auto s = SherpaOnnxCreateOfflineStream(offline_);
  if (!s)
    throw std::runtime_error("Cannot create offline stream");
  SherpaOnnxAcceptWaveformOffline(s, 16000, samples.data(), samples.size());
  SherpaOnnxDecodeOfflineStream(offline_, s);
  auto r = SherpaOnnxGetOfflineStreamResult(s);
  std::string text = r && r->text ? r->text : "";
  if (r)
    SherpaOnnxDestroyOfflineRecognizerResult(r);
  SherpaOnnxDestroyOfflineStream(s);
  return text;
}
Transcript Asr::finish(const std::atomic<bool> *cancelled) {
  Transcript result;
  if (fun_) {
    if (!recording_)
      return result;
    try {
      result = fun_->decode(samples_, cancelled);
    } catch (...) {
      cancel();
      throw;
    }
    cancel();
    return result;
  }
  if (!stream_)
    return result;
  auto intervals = speech_->locate(samples_);
  std::cerr << "Recognition diagnostic: audio_s=" << samples_.size() / 16000.0
            << " speech_regions=" << intervals.size();
  for (auto interval : intervals)
    std::cerr << " [" << interval.begin / 16000.0 << ','
              << interval.end / 16000.0 << ']';
  std::cerr << '\n';
  result.speech = !intervals.empty();
  auto recorded_hypothesis = current();
  std::vector<float> tail(20480, 0);
  SherpaOnnxOnlineStreamAcceptWaveform(stream_, 16000, tail.data(),
                                       tail.size());
  SherpaOnnxOnlineStreamInputFinished(stream_);
  while (SherpaOnnxIsOnlineStreamReady(online_, stream_))
    SherpaOnnxDecodeOnlineStream(online_, stream_);
  result.streaming = current();
  if (!result.speech) {
    // A negative VAD result is not proof that an existing ASR hypothesis is
    // empty. Retain it for explicit review, without auto-pasting possible noise.
    // Padding itself can produce a stray character on pure silence. Use only
    // the existing hypothesis from real audio, and ignore tiny noise fragments.
    result.streaming = recorded_hypothesis;
    if (g_utf8_strlen(recorded_hypothesis.c_str(), -1) >= 4) {
      result.text = recorded_hypothesis;
      result.warning = "语音检测未确认讲话，已保留识别文字；请确认后插入或取消";
    }
    std::cerr << "Recognition diagnostic: no confirmed speech; retained_chars="
              << g_utf8_strlen(result.text.c_str(), -1) << '\n';
    cancel();
    return result;
  }
  result.text = result.streaming;
  try {
    std::vector<std::string> utterances;
    auto jobs = RecognitionJobs(samples_, intervals);
    for (const auto &job : jobs) {
      auto text = decodeOffline(job);
      if (text.empty())
        throw std::runtime_error("Offline recognition left an utterance empty");
      utterances.push_back(std::move(text));
    }
    auto refined = AssembleTranscript(utterances);
    if (!refined.empty() && PreservesTranscriptLength(result.streaming, refined))
      result.text = std::move(refined);
    else if (!refined.empty())
      std::cerr << "Refinement diagnostic: shortened result rejected; "
                   "preserving streaming result\n";
  } catch (const std::exception &e) {
    std::cerr << "Refinement failed; preserving streaming result: " << e.what()
              << '\n';
  }
  cancel();
  std::cerr << "Recognition diagnostic: streaming_chars="
            << g_utf8_strlen(result.streaming.c_str(), -1)
            << " final_chars=" << g_utf8_strlen(result.text.c_str(), -1) << '\n';
  return result;
}
void Asr::cancel() {
  if (stream_)
    SherpaOnnxDestroyOnlineStream(stream_);
  stream_ = nullptr;
  recording_ = false;
  samples_.clear();
}
std::vector<float> ReadWave(const std::string &p) {
  auto wave = SherpaOnnxReadWave(p.c_str());
  if (!wave)
    throw std::runtime_error("Cannot read WAV: " + p);
  if (wave->sample_rate != 16000) {
    SherpaOnnxFreeWave(wave);
    throw std::runtime_error("WAV must be mono 16 kHz");
  }
  std::vector<float> pcm(wave->samples, wave->samples + wave->num_samples);
  SherpaOnnxFreeWave(wave);
  return pcm;
}
} // namespace hv
