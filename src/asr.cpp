// Quiet-boundary audio splitting adapted from the Mobius fcitx5-vinput fork.
// Modified for Hyprvoice on 2026-10-03; GPL-3.0, see NOTICE.
#include "asr.h"
#include "text_join.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glib.h>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
namespace hv {
std::vector<std::vector<float>> SplitAudio(const std::vector<float> &pcm) {
  constexpr size_t max = 16000 * 30, window = 3200, search = 16000 * 3;
  std::vector<std::vector<float>> chunks;
  size_t begin = 0;
  while (pcm.size() - begin > max) {
    size_t nominal = begin + max, lo = nominal - search,
           hi = std::min(pcm.size() - window, nominal + search), cut = nominal;
    double best = std::numeric_limits<double>::max();
    for (size_t pos = lo; pos + window <= hi; pos += window / 2) {
      double energy = 0;
      for (size_t i = pos; i < pos + window; ++i)
        energy += double(pcm[i]) * pcm[i];
      if (energy < best) {
        best = energy;
        cut = pos;
      }
    }
    chunks.emplace_back(pcm.begin() + begin, pcm.begin() + cut);
    begin = cut;
  }
  if (begin < pcm.size())
    chunks.emplace_back(pcm.begin() + begin, pcm.end());
  return chunks;
}
static std::string Asset(const std::string &dir, const std::string &name) {
  auto p = std::filesystem::path(dir) / name;
  if (!std::filesystem::is_regular_file(p))
    throw std::runtime_error("Model file missing: " + p.string());
  return p.string();
}
Asr::Asr(const Config &c) {
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
    std::string error;
    if (!vad_.Init(c.path("vad_model"), 16000, "cpu", {}, &error))
      throw std::runtime_error(error);
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
void Asr::begin() {
  cancel();
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
  if (!stream_)
    throw std::runtime_error("No recognition session");
  samples_.insert(samples_.end(), samples.begin(), samples.end());
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
Transcript Asr::finish() {
  Transcript result;
  if (!stream_)
    return result;
  vad_.Trim(samples_, 16000);
  result.speech = vad_.DetectedSpeech();
  if (!result.speech || samples_.size() < 3200) {
    cancel();
    return result;
  }
  std::vector<float> tail(20480, 0);
  SherpaOnnxOnlineStreamAcceptWaveform(stream_, 16000, tail.data(),
                                       tail.size());
  SherpaOnnxOnlineStreamInputFinished(stream_);
  while (SherpaOnnxIsOnlineStreamReady(online_, stream_))
    SherpaOnnxDecodeOnlineStream(online_, stream_);
  result.streaming = current();
  result.text = result.streaming;
  try {
    std::string refined;
    bool complete = true;
    for (auto &chunk : SplitAudio(samples_)) {
      auto trimmed = vad_.Trim(chunk, 16000);
      if (!vad_.DetectedSpeech() || trimmed.size() < 3200)
        continue;
      std::vector<std::vector<float>> parts;
      if (chunk.size() > 16000 * 20 && vad_.SpeechRanges().size() > 1)
        for (auto [a, b] : vad_.SpeechRanges())
          parts.emplace_back(chunk.begin() + a, chunk.begin() + b);
      else
        parts.push_back(std::move(trimmed));
      for (auto &part : parts) {
        auto text = decodeOffline(part);
        if (text.empty()) {
          complete = false;
          break;
        }
        vinput::daemon::asr::AppendRecognizedText(refined, text);
      }
      if (!complete)
        break;
    }
    if (complete && !refined.empty())
      result.text = refined;
  } catch (const std::exception &e) {
    std::cerr << "Refinement failed; preserving streaming result: " << e.what()
              << '\n';
  }
  cancel();
  return result;
}
void Asr::cancel() {
  if (stream_)
    SherpaOnnxDestroyOnlineStream(stream_);
  stream_ = nullptr;
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
