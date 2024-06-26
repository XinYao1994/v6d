/** Copyright 2020-2023 Alibaba Group Holding Limited.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include <unistd.h>
#include <iostream>
#include <random>
#include <vector>

#include "gulrak/filesystem.hpp"
#include "llm-cache/ds/config.h"
#include "llm-cache/ds/kv_state_cache_manager.h"
#include "rax/radix.h"

using namespace vineyard;  // NOLINT(build/namespaces)

int tensorBytes = 80;
int capacity = 20;
int layer = 3;

FileCacheConfig config;

std::vector<int> round_1_tokens = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
    55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69};
std::vector<int> round_2_tokens = {1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14};
std::vector<int> round_3_tokens = {1, 2, 3, 9, 10, 11, 12, 13, 14};
std::vector<int> round_4_tokens = {1, 2, 3, 4, 5, 6};

std::vector<std::vector<int>> tokens_list = {round_1_tokens, round_2_tokens,
                                             round_3_tokens, round_4_tokens};

std::shared_ptr<KVStateCacheManager> init() {
  std::shared_ptr<KVStateCacheManager> kv_state_cache_manager;
  VINEYARD_CHECK_OK(KVStateCacheManager::Make(kv_state_cache_manager, config));
  return kv_state_cache_manager;
}

void print_current_tokens(const std::vector<int>& prefix, int next_token) {
  std::string tokens_str = "";
  for (size_t i = 0; i < prefix.size(); ++i) {
    tokens_str += std::to_string(prefix[i]) + " ";
  }
  tokens_str += std::to_string(next_token);
  LOG(INFO) << "Current tokens: " + tokens_str;
}

void print_kv_state(const std::vector<std::pair<LLMKV, LLMKV>>& kv_state) {
  LOG(INFO) << "kv_state: ";
  for (size_t i = 0; i < kv_state.size(); ++i) {
    uint8_t* key_state_data =
        reinterpret_cast<uint8_t*>(kv_state[i].first.data);
    uint8_t* value_state_data =
        reinterpret_cast<uint8_t*>(kv_state[i].second.data);
    // print the first tensorBytes bytes
    std::string key_state_str = "";
    std::string value_state_str = "";
    for (int j = 0; j < tensorBytes; j++) {
      key_state_str += std::to_string(key_state_data[j]) + " ";
      value_state_str += std::to_string(value_state_data[j]) + " ";
    }
    LOG(INFO) << "layer " << i << ":";
    LOG(INFO) << "key_state: " << key_state_str;
    LOG(INFO) << "value_state: " << value_state_str;
    LOG(INFO) << "---------------------";
  }
}

// we do not consider the layer.
std::vector<std::pair<LLMKV, LLMKV>> generate_kv_state(int token) {
  std::vector<std::pair<LLMKV, LLMKV>> kv_state;
  for (int currentLayer = 0; currentLayer < layer; currentLayer++) {
    LLMKV key_state;
    LLMKV value_state;
    key_state.data = malloc(tensorBytes);
    value_state.data = malloc(tensorBytes);

    key_state.length = tensorBytes;
    value_state.length = tensorBytes;

    for (int i = 0; i < tensorBytes; ++i) {
      (reinterpret_cast<uint8_t*>(key_state.data))[i] =
          (static_cast<uint8_t>(token)) + i + currentLayer;
      (reinterpret_cast<uint8_t*>(value_state.data))[i] =
          (static_cast<uint8_t>(token)) + i + currentLayer;
    }

    kv_state.emplace_back(key_state, value_state);
  }
  return kv_state;
}

void check_kv_state(const std::vector<std::pair<LLMKV, LLMKV>>& kv_state,
                    int& token) {
  VINEYARD_ASSERT(kv_state.size() == (size_t) layer);
  for (size_t index = 0; index < kv_state.size(); ++index) {
    LOG(INFO) << "kv_state length: " << kv_state[index].first.length
              << "tensorBytes: " << tensorBytes << "layer: " << layer;
    VINEYARD_ASSERT(kv_state[index].first.length == (size_t) tensorBytes);
    VINEYARD_ASSERT(kv_state[index].second.length == (size_t) tensorBytes);
    for (int i = 0; i < tensorBytes; ++i) {
      if ((reinterpret_cast<uint8_t*>(kv_state[index].first.data))[i] !=
          (static_cast<uint8_t>(token)) + i + index) {
        LOG(INFO) << "token:" << token << " tensorBytes" << tensorBytes
                  << " layer:" << index;
        LOG(INFO) << "key_state[" << i << "]: "
                  << (reinterpret_cast<uint8_t*>(kv_state[index].first.data))[i]
                  << ". But is should be "
                  << (static_cast<uint8_t>(token)) + i + index;
        throw std::runtime_error("key_state error!");
      }
      if (reinterpret_cast<uint8_t*>(kv_state[index].second.data)[i] !=
          (static_cast<uint8_t>(token)) + i + index) {
        LOG(INFO) << "token:" << token << " tensorBytes" << tensorBytes
                  << " layer:" << index;
        LOG(INFO) << "value_state[" << i << "]: "
                  << (reinterpret_cast<uint8_t*>(
                         kv_state[index].second.data))[i]
                  << ". But is should be "
                  << (static_cast<uint8_t>(token)) + i + index;
        throw std::runtime_error("value_state error!");
      }
    }
  }
}

void inference(std::shared_ptr<KVStateCacheManager>& kv_state_cache_manager,
               std::vector<int> tokens, bool block = false) {
  std::vector<int> inference_tokens;
  std::vector<std::vector<std::pair<LLMKV, LLMKV>>> kv_state;
  for (size_t i = 0; i < tokens.size(); ++i) {
    std::vector<std::pair<LLMKV, LLMKV>> current_kv_state =
        generate_kv_state(tokens[i]);
    print_kv_state(current_kv_state);
    kv_state.push_back(current_kv_state);
    inference_tokens.push_back(tokens[i]);
  }

  size_t updated = 0;
  Status result =
      kv_state_cache_manager->Update(inference_tokens, kv_state, updated);

  std::vector<std::vector<std::pair<LLMKV, LLMKV>>> kv_state_to_query;
  for (size_t i = 0; i < tokens.size(); ++i) {
    std::vector<std::pair<LLMKV, LLMKV>> current_kv_state =
        generate_kv_state(0);
    kv_state_to_query.push_back(current_kv_state);
  }
  size_t matched = 0;
  Status query_result = kv_state_cache_manager->Query(
      inference_tokens, kv_state_to_query, matched);
  if (!query_result.ok()) {
    LOG(INFO) << "Query failed!";
  }

  LOG(INFO) << "Match tokens:" << matched << ". Total tokens:" << tokens.size();
  for (size_t i = 0; i < matched; ++i) {
    check_kv_state(kv_state_to_query[i], tokens[i]);
  }
}

void checkFilesNotExist(std::string dir) {
  for (auto it = ghc::filesystem::recursive_directory_iterator(dir);
       it != ghc::filesystem::recursive_directory_iterator(); ++it) {
    VINEYARD_ASSERT(!ghc::filesystem::is_regular_file(*it));
  }
}

void threadFunc(int sleep_time) {
  std::shared_ptr<KVStateCacheManager> manager = init();

  for (size_t i = 0; i < tokens_list.size(); i++) {
    LOG(INFO) << "Round " << i << " :";
    inference(manager, tokens_list[i]);
  }

  LOG(INFO) << "inference end";
  sleep(sleep_time);
  manager->Close();
}

int main(int argc, char** argv) {
  LOG(INFO) << "Test KVStateCache with tensorBytes: " << tensorBytes
            << ", capacity: " << capacity << ", layer: " << layer;

  config = FileCacheConfig(tensorBytes, capacity, layer, 4, 2,
                           "/tmp/llm_cache/", LOCAL, 1, 1, false, 3, 5);

  std::vector<std::thread> threads;
  for (int i = 0; i < 1; i++) {
    threads.push_back(std::thread(threadFunc, 3));
  }

  for (int i = 0; i < 1; i++) {
    threads[i].join();
    LOG(INFO) << "Thread:" << i << " exit.";
  }

  checkFilesNotExist("/tmp/llm_cache/");

  config = FileCacheConfig(tensorBytes, capacity, layer, 4, 2,
                           "/tmp/llm_cache/", LOCAL, 10, 20, true, 1, 2);

  threads.clear();
  for (int i = 0; i < 1; i++) {
    threads.push_back(std::thread(threadFunc, 0));
  }

  for (int i = 0; i < 1; i++) {
    threads[i].join();
    LOG(INFO) << "Thread:" << i << " exit.";
  }

  sleep(3);
  checkFilesNotExist("/tmp/llm_cache/");

  LOG(INFO) << "Passed KVStateCache tests...";
  return 0;
}
