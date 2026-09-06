// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "mbo/digest/digest.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Known-answer vectors: the FIPS 180-4 / NIST CAVP examples plus
// padding-boundary lengths (55/56/63/64/65 bytes straddle the 0x80+length
// padding cases). Every hex value was generated with python's hashlib, whose
// implementations are independently NIST-validated.
namespace mbo::digest {
namespace {

using ::testing::ElementsAreArray;

static_assert(IsDigestAlgorithm<sha256::Algorithm>);
static_assert(IsDigestAlgorithm<sha224::Algorithm>);
static_assert(HasStreaming<sha256::Algorithm>);
static_assert(HasStreaming<sha224::Algorithm>);

constexpr uint8_t Nibble(char chr) noexcept {
  if (chr >= '0' && chr <= '9') {
    return static_cast<uint8_t>(chr - '0');
  }
  return static_cast<uint8_t>(chr - 'a' + 10);  // NOLINT(*-magic-numbers)
}

// Parses "ab01..." into bytes at compile time (test-side inverse of
// `ToHexString`).
template<std::size_t DigestSize>
constexpr std::array<uint8_t, DigestSize> FromHex(std::string_view hex) noexcept {
  std::array<uint8_t, DigestSize> result = {};
  if (hex.size() != 2 * DigestSize) {
    return result;  // Wrong-length vector: returns zeros, which cannot match a real digest.
  }
  for (std::size_t i = 0; i < DigestSize; ++i) {
    result.at(i) = static_cast<uint8_t>(
        (static_cast<uint32_t>(Nibble(hex.at(2 * i))) << 4U) | static_cast<uint32_t>(Nibble(hex.at((2 * i) + 1))));
  }
  return result;
}

struct TestVector {
  std::string_view input;
  std::string_view hex;
};

// 200 'a's; the boundary-length inputs below are prefixes of this (block
// sizes range from 64 to the SHAKE128 rate of 168).
constexpr std::string_view kManyAs =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

constexpr std::string_view kFoxMsg = "The quick brown fox jumps over the lazy dog";

// 200 'k's; the block-boundary keys below are prefixes of this.
constexpr std::string_view kManyKs =
    "kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk"
    "kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk"
    "kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk";

constexpr std::string_view kTwoBlockMsg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
constexpr std::string_view kFourBlockMsg =
    "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

// The fixed output size used for the SHAKE XOFs in the typed suite.
constexpr std::size_t kShakeTestOutput = 32;

template<typename Algo>
struct AlgoTraits;

template<>
struct AlgoTraits<sha256::Algorithm> {
  static constexpr std::string_view kName = "sha256";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {.input = "abc", .hex = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {.input = kTwoBlockMsg, .hex = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
      {.input = kFourBlockMsg, .hex = "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
      {.input = std::string_view{"\0", 1}, .hex = "6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d"},
      {.input = kManyAs.substr(0, 55), .hex = "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
      {.input = kManyAs.substr(0, 56), .hex = "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
      {.input = kManyAs.substr(0, 63), .hex = "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
      {.input = kManyAs.substr(0, 64), .hex = "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
      {.input = kManyAs.substr(0, 65), .hex = "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
  });
  static constexpr std::string_view kMillionAHex = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
};

template<>
struct AlgoTraits<sha224::Algorithm> {
  static constexpr std::string_view kName = "sha224";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f"},
      {.input = "abc", .hex = "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7"},
      {.input = kTwoBlockMsg, .hex = "75388b16512776cc5dba5da1fd890150b0c6455cb4f58b1952522525"},
      {.input = kFourBlockMsg, .hex = "c97ca9a559850ce97a04a96def6d99a9e0e0e2ab14e6b8df265fc0b3"},
      {.input = std::string_view{"\0", 1}, .hex = "fff9292b4201617bdc4d3053fce02734166a683d7d858a7f5f59b073"},
      {.input = kManyAs.substr(0, 55), .hex = "fb0bd626a70c28541dfa781bb5cc4d7d7f56622a58f01a0b1ddd646f"},
      {.input = kManyAs.substr(0, 56), .hex = "d40854fc9caf172067136f2e29e1380b14626bf6f0dd06779f820dcd"},
      {.input = kManyAs.substr(0, 63), .hex = "1d4e051f4d6fed2a63fd2421e65834cec00d64456553de3496ae8b1d"},
      {.input = kManyAs.substr(0, 64), .hex = "a88cd5cde6d6fe9136a4e58b49167461ea95d388ca2bdb7afdc3cbf4"},
      {.input = kManyAs.substr(0, 65), .hex = "ff8716f600af42959d0efb52e1f21b01bb328733009344d511c299fb"},
  });
  static constexpr std::string_view kMillionAHex = "20794655980c91d8bbb4c1ea97618a4bf03f42581948b2ee4ee7ad67";
};

template<>
struct AlgoTraits<sha1::Algorithm> {
  static constexpr std::string_view kName = "sha1";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
      {.input = "abc", .hex = "a9993e364706816aba3e25717850c26c9cd0d89d"},
      {.input = kTwoBlockMsg, .hex = "84983e441c3bd26ebaae4aa1f95129e5e54670f1"},
      {.input = kFourBlockMsg, .hex = "a49b2446a02c645bf419f995b67091253a04a259"},
      {.input = std::string_view{"\0", 1}, .hex = "5ba93c9db0cff93f52b521d7420e43f6eda2784f"},
      {.input = kManyAs.substr(0, 55), .hex = "c1c8bbdc22796e28c0e15163d20899b65621d65a"},
      {.input = kManyAs.substr(0, 56), .hex = "c2db330f6083854c99d4b5bfb6e8f29f201be699"},
      {.input = kManyAs.substr(0, 63), .hex = "03f09f5b158a7a8cdad920bddc29b81c18a551f5"},
      {.input = kManyAs.substr(0, 64), .hex = "0098ba824b5c16427bd7a1122a5a442a25ec644d"},
      {.input = kManyAs.substr(0, 65), .hex = "11655326c708d70319be2610e8a57d9a5b959d3b"},
  });
  static constexpr std::string_view kMillionAHex = "34aa973cd4c4daa4f61eeb2bdbad27316534016f";
};

template<>
struct AlgoTraits<sha512::Algorithm> {
  static constexpr std::string_view kName = "sha512";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "",
       .hex = "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd"
              "47417a81a538327af927da3e"},
      {.input = "abc",
       .hex = "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423"
              "643ce80e2a9ac94fa54ca49f"},
      {.input = kTwoBlockMsg,
       .hex = "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c33596fd15c13b1b07f9aa1d3bea57789ca031ad85c7"
              "a71dd70354ec631238ca3445"},
      {.input = kFourBlockMsg,
       .hex = "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329ee"
              "b6dd26545e96e55b874be909"},
      {.input = std::string_view{"\0", 1},
       .hex = "b8244d028981d693af7b456af8efa4cad63d282e19ff14942c246e50d9351d22704a802a71c3580b6370de4ceb293c324a842334"
              "2557d4e5c38438f0e36910ee"},
      {.input = kManyAs.substr(0, 111),
       .hex = "fa9121c7b32b9e01733d034cfc78cbf67f926c7ed83e82200ef86818196921760b4beff48404df811b953828274461673c68d04e"
              "297b0eb7b2b4d60fc6b566a2"},
      {.input = kManyAs.substr(0, 112),
       .hex = "c01d080efd492776a1c43bd23dd99d0a2e626d481e16782e75d54c2503b5dc32bd05f0f1ba33e568b88fd2d970929b719ecbb152"
              "f58f130a407c8830604b70ca"},
      {.input = kManyAs.substr(0, 127),
       .hex = "828613968b501dc00a97e08c73b118aa8876c26b8aac93df128502ab360f91bab50a51e088769a5c1eff4782ace147dce3642554"
              "199876374291f5d921629502"},
      {.input = kManyAs.substr(0, 128),
       .hex = "b73d1929aa615934e61a871596b3f3b33359f42b8175602e89f7e06e5f658a243667807ed300314b95cacdd579f3e33abdfbe351"
              "909519a846d465c59582f321"},
      {.input = kManyAs.substr(0, 129),
       .hex = "4f681e0bd53cda4b5a2041cc8a06f2eabde44fb16c951fbd5b87702f07aeab611565b19c47fde30587177ebb852e3971bbd8d3fd"
              "30da18d71037dfbd98420429"},
  });
  static constexpr std::string_view kMillionAHex =
      "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e"
      "4eadb217ad8cc09b";
};

template<>
struct AlgoTraits<sha384::Algorithm> {
  static constexpr std::string_view kName = "sha384";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "",
       .hex = "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b"},
      {.input = "abc",
       .hex = "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7"},
      {.input = kTwoBlockMsg,
       .hex = "3391fdddfc8dc7393707a65b1b4709397cf8b1d162af05abfe8f450de5f36bc6b0455a8520bc4e6f5fe95b1fe3c8452b"},
      {.input = kFourBlockMsg,
       .hex = "09330c33f71147e83d192fc782cd1b4753111b173b3b05d22fa08086e3b0f712fcc7c71a557e2db966c3e9fa91746039"},
      {.input = std::string_view{"\0", 1},
       .hex = "bec021b4f368e3069134e012c2b4307083d3a9bdd206e24e5f0d86e13d6636655933ec2b413465966817a9c208a11717"},
      {.input = kManyAs.substr(0, 111),
       .hex = "3c37955051cb5c3026f94d551d5b5e2ac38d572ae4e07172085fed81f8466b8f90dc23a8ffcdea0b8d8e58e8fdacc80a"},
      {.input = kManyAs.substr(0, 112),
       .hex = "187d4e07cb306103c69967bf544d0dfbe9042577599c73c330abc0cb64c61236d5ed565ee19119d8c31779a38f791fcd"},
      {.input = kManyAs.substr(0, 127),
       .hex = "9bd06b1763c2cf7aef40e795dc65bc96d59c41b537f3ad72ebdefd485476b5717c1aeb37c327fe9c1831b12b9efd08ae"},
      {.input = kManyAs.substr(0, 128),
       .hex = "edb12730a366098b3b2beac75a3bef1b0969b15c48e2163c23d96994f8d1bef760c7e27f3c464d3829f56c0d53808b0b"},
      {.input = kManyAs.substr(0, 129),
       .hex = "39b6f5a7b0e781dbc419f72e49b30eaac10f2c98c4403bc610da31067fd1b48f324138c8615d2b496d08d73d5e865326"},
  });
  static constexpr std::string_view kMillionAHex =
      "9d0e1809716474cb086e834e310a4a1ced149e9c00f248527972cec5704c2a5b07b8b3dc38ecc4ebae97ddd87f3d8985";
};

template<>
struct AlgoTraits<sha512_224::Algorithm> {
  static constexpr std::string_view kName = "sha512_224";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "6ed0dd02806fa89e25de060c19d3ac86cabb87d6a0ddd05c333b84f4"},
      {.input = "abc", .hex = "4634270f707b6a54daae7530460842e20e37ed265ceee9a43e8924aa"},
      {.input = kTwoBlockMsg, .hex = "e5302d6d54bb242275d1e7622d68df6eb02dedd13f564c13dbda2174"},
      {.input = kFourBlockMsg, .hex = "23fec5bb94d60b23308192640b0c453335d664734fe40e7268674af9"},
      {.input = std::string_view{"\0", 1}, .hex = "283bb59af7081ed08197227d8f65b9591ffe1155be43e9550e57f941"},
      {.input = kManyAs.substr(0, 111), .hex = "3ebe1b48e8c66acb9ae014db95b4bec93de7e9572bff41cf566bd7d0"},
      {.input = kManyAs.substr(0, 112), .hex = "79b41fef2a0439d2705724a67615f7bcbcd2bf5664a7774b80818eb6"},
      {.input = kManyAs.substr(0, 127), .hex = "65aec5ddd181bb86e1921d493a0667492cb8dbc2b560ec061ed2c492"},
      {.input = kManyAs.substr(0, 128), .hex = "261b94bcba554264b3b738e9e09e7dc68ac8e0b4c8517fe9bb7c3617"},
      {.input = kManyAs.substr(0, 129), .hex = "3a19e0ab45e58ffb1db38df972ac85842bff2bbacd16ec9819a6a434"},
  });
  static constexpr std::string_view kMillionAHex = "37ab331d76f0d36de422bd0edeb22a28accd487b7a8453ae965dd287";
};

template<>
struct AlgoTraits<sha512_256::Algorithm> {
  static constexpr std::string_view kName = "sha512_256";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "c672b8d1ef56ed28ab87c3622c5114069bdd3ad7b8f9737498d0c01ecef0967a"},
      {.input = "abc", .hex = "53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23"},
      {.input = kTwoBlockMsg, .hex = "bde8e1f9f19bb9fd3406c90ec6bc47bd36d8ada9f11880dbc8a22a7078b6a461"},
      {.input = kFourBlockMsg, .hex = "3928e184fb8690f840da3988121d31be65cb9d3ef83ee6146feac861e19b563a"},
      {.input = std::string_view{"\0", 1}, .hex = "10baad1713566ac2333467bddb0597dec9066120dd72ac2dcb8394221dcbe43d"},
      {.input = kManyAs.substr(0, 111), .hex = "0239e429f98d0ed61ee8e2a7c30afe98c1c3a80ce5dff62a107e9c538f7632ce"},
      {.input = kManyAs.substr(0, 112), .hex = "9216b5303edb66504570bee90e48ea5beaa5e9fe9f760bbd3e0460559fc005f6"},
      {.input = kManyAs.substr(0, 127), .hex = "2fe3b2a6ee7e12f6fe4ba82166541ad9b4ed882c493581cbe300d68f3757b778"},
      {.input = kManyAs.substr(0, 128), .hex = "b88f97e274f9c1d49f181c8cbd01a9c74930ad055a46ac4499a1d601f1c80bf2"},
      {.input = kManyAs.substr(0, 129), .hex = "fb9035c9009ed4a60e37510339ebdb1c771339f30aa581d5dea3690a524c23f1"},
  });
  static constexpr std::string_view kMillionAHex = "9a59a052930187a97038cae692f30708aa6491923ef5194394dc68d56c74fb21";
};

template<>
struct AlgoTraits<md5::Algorithm> {
  static constexpr std::string_view kName = "md5";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "d41d8cd98f00b204e9800998ecf8427e"},
      {.input = "abc", .hex = "900150983cd24fb0d6963f7d28e17f72"},
      {.input = kTwoBlockMsg, .hex = "8215ef0796a20bcaaae116d3876c664a"},
      {.input = kFourBlockMsg, .hex = "03dd8807a93175fb062dfb55dc7d359c"},
      {.input = std::string_view{"\0", 1}, .hex = "93b885adfe0da089cdf634904fd59f71"},
      {.input = kManyAs.substr(0, 55), .hex = "ef1772b6dff9a122358552954ad0df65"},
      {.input = kManyAs.substr(0, 56), .hex = "3b0c8ac703f828b04c6c197006d17218"},
      {.input = kManyAs.substr(0, 63), .hex = "b06521f39153d618550606be297466d5"},
      {.input = kManyAs.substr(0, 64), .hex = "014842d480b571495a4a0363793f7367"},
      {.input = kManyAs.substr(0, 65), .hex = "c743a45e0d2e6a95cb859adae0248435"},
  });
  static constexpr std::string_view kMillionAHex = "7707d6ae4e027c70eea2a935c2296f21";
};

template<>
struct AlgoTraits<sha3_224::Algorithm> {
  static constexpr std::string_view kName = "sha3_224";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "6b4e03423667dbb73b6e15454f0eb1abd4597f9a1b078e3f5b5a6bc7"},
      {.input = "abc", .hex = "e642824c3f8cf24ad09234ee7d3c766fc9a3a5168d0c94ad73b46fdf"},
      {.input = kTwoBlockMsg, .hex = "8a24108b154ada21c9fd5574494479ba5c7e7ab76ef264ead0fcce33"},
      {.input = kFourBlockMsg, .hex = "543e6868e1666c1a643630df77367ae5a62a85070a51c14cbf665cbc"},
      {.input = std::string_view{"\0", 1}, .hex = "bdd5167212d2dc69665f5a8875ab87f23d5ce7849132f56371a19096"},
      {.input = kManyAs.substr(0, 143), .hex = "73b1b22b54f515f626a6abdde6af25cd4801dc6e9dc7fa3f77e1c122"},
      {.input = kManyAs.substr(0, 144), .hex = "f9019111996dcf160e284e320fd6d8825cabcd41a5ffdc4c5e9d64b6"},
      {.input = kManyAs.substr(0, 145), .hex = "7f0521c84aeacc8a46aba17171acbdd22522509a71c663257fbdee0e"},
  });
  static constexpr std::string_view kMillionAHex = "d69335b93325192e516a912e6d19a15cb51c6ed5c15243e7a7fd653c";
};

template<>
struct AlgoTraits<sha3_256::Algorithm> {
  static constexpr std::string_view kName = "sha3_256";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"},
      {.input = "abc", .hex = "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"},
      {.input = kTwoBlockMsg, .hex = "41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376"},
      {.input = kFourBlockMsg, .hex = "916f6061fe879741ca6469b43971dfdb28b1a32dc36cb3254e812be27aad1d18"},
      {.input = std::string_view{"\0", 1}, .hex = "5d53469f20fef4f8eab52b88044ede69c77a6a68a60728609fc4a65ff531e7d0"},
      {.input = kManyAs.substr(0, 135), .hex = "8094bb53c44cfb1e67b7c30447f9a1c33696d2463ecc1d9c92538913392843c9"},
      {.input = kManyAs.substr(0, 136), .hex = "3fc5559f14db8e453a0a3091edbd2bc25e11528d81c66fa570a4efdcc2695ee1"},
      {.input = kManyAs.substr(0, 137), .hex = "f8d6846cedd2ccfadf15c5879ef95af724d799eed7391fb1c91f95344e738614"},
  });
  static constexpr std::string_view kMillionAHex = "5c8875ae474a3634ba4fd55ec85bffd661f32aca75c6d699d0cdcb6c115891c1";
};

template<>
struct AlgoTraits<sha3_384::Algorithm> {
  static constexpr std::string_view kName = "sha3_384";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "",
       .hex = "0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2ac3713831264adb47fb6bd1e058d5f004"},
      {.input = "abc",
       .hex = "ec01498288516fc926459f58e2c6ad8df9b473cb0fc08c2596da7cf0e49be4b298d88cea927ac7f539f1edf228376d25"},
      {.input = kTwoBlockMsg,
       .hex = "991c665755eb3a4b6bbdfb75c78a492e8c56a22c5c4d7e429bfdbc32b9d4ad5aa04a1f076e62fea19eef51acd0657c22"},
      {.input = kFourBlockMsg,
       .hex = "79407d3b5916b59c3e30b09822974791c313fb9ecc849e406f23592d04f625dc8c709b98b43b3852b337216179aa7fc7"},
      {.input = std::string_view{"\0", 1},
       .hex = "127677f8b66725bbcb7c3eae9698351ca41e0eb6d66c784bd28dcdb3b5fb12d0c8e840342db03ad1ae180b92e3504933"},
      {.input = kManyAs.substr(0, 103),
       .hex = "af61fb4fd1c6afe80857fcba888318a0a1426635b4509f09707e3787630bdb621655ffa54f5884088ccc000f81436414"},
      {.input = kManyAs.substr(0, 104),
       .hex = "3a4f3b6284e571238884e95655e8c8a60e068e4059a9734abc08823a900d161592860243f00619ae699a29092ed91a16"},
      {.input = kManyAs.substr(0, 105),
       .hex = "cb73ab2f8f5fbb13f0e115a7062ba1644aa16534aa80d076ef27f8550deb900d89bdfa169b45073223acadb6001204d3"},
  });
  static constexpr std::string_view kMillionAHex =
      "eee9e24d78c1855337983451df97c8ad9eedf256c6334f8e948d252d5e0e76847aa0774ddb90a842190d2c558b4b8340";
};

template<>
struct AlgoTraits<sha3_512::Algorithm> {
  static constexpr std::string_view kName = "sha3_512";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "",
       .hex = "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f500199d"
              "95b6d3e301758586281dcd26"},
      {.input = "abc",
       .hex = "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4c"
              "f408d5a56592f8274eec53f0"},
      {.input = kTwoBlockMsg,
       .hex = "04a371e84ecfb5b8b77cb48610fca8182dd457ce6f326a0fd3d7ec2f1e91636dee691fbe0c985302ba1b0d8dc78c086346b533b4"
              "9c030d99a27daf1139d6e75e"},
      {.input = kFourBlockMsg,
       .hex = "afebb2ef542e6579c50cad06d2e578f9f8dd6881d7dc824d26360feebf18a4fa73e3261122948efcfd492e74e82e2189ed0fb440"
              "d187f382270cb455f21dd185"},
      {.input = std::string_view{"\0", 1},
       .hex = "7127aab211f82a18d06cf7578ff49d5089017944139aa60d8bee057811a15fb55a53887600a3eceba004de51105139f32506fe5b"
              "53e1913bfa6b32e716fe97da"},
      {.input = kManyAs.substr(0, 71),
       .hex = "070faf98d2a8fddf8ed886408744dc06456096c2e045f26f3c7b010530e6bbb3db535a54d636856f4e0e1e982461cb9a7e8e57ff"
              "8895cff1619af9f0e486e28c"},
      {.input = kManyAs.substr(0, 72),
       .hex = "a8ae722a78e10cbbc413886c02eb5b369a03f6560084aff566bd597bb7ad8c1ccd86e81296852359bf2faddb5153c0a744572298"
              "7875e74287adac21adebe952"},
      {.input = kManyAs.substr(0, 73),
       .hex = "23e6a8815f8201dbbf6a5463be8dcadb1acea9df5f8998954e59ac9565cf6d29b17aa27a5e8b0fc06343db6122d6e544d27583dd"
              "c78504d08203217e7e65b6bd"},
  });
  static constexpr std::string_view kMillionAHex =
      "3c3a876da14034ab60627c077bb98f7e120a2a5370212dffb3385a18d4f38859ed311d0a9d5141ce9cc5c66ee689b266a8aa18ace8282a0e"
      "0db596c90b0a7b87";
};

template<>
struct AlgoTraits<blake2b::Algorithm> {
  static constexpr std::string_view kName = "blake2b";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "",
       .hex = "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419d25e1031afee585313896444934eb04b903a685b"
              "1448b755d56f701afe9be2ce"},
      {.input = "abc",
       .hex = "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8"
              "dbf1925ab92386edd4009923"},
      {.input = kTwoBlockMsg,
       .hex = "7285ff3e8bd768d69be62b3bf18765a325917fa9744ac2f582a20850bc2b1141ed1b3e4528595acc90772bdf2d37dc8a47130b44"
              "f33a02e8730e5ad8e166e888"},
      {.input = kFourBlockMsg,
       .hex = "ce741ac5930fe346811175c5227bb7bfcd47f42612fae46c0809514f9e0e3a11ee1773287147cdeaeedff50709aa716341fe6524"
              "0f4ad6777d6bfaf9726e5e52"},
      {.input = std::string_view{"\0", 1},
       .hex = "2fa3f686df876995167e7c2e5d74c4c7b6e48f8068fe0e44208344d480f7904c36963e44115fe3eb2a3ac8694c28bcb4f5a0f327"
              "6f2e79487d8219057a506e4b"},
      {.input = kManyAs.substr(0, 127),
       .hex = "94596b9d6199c807c40ae1a935f3633ba5a8dd5655f7f1bd44f5285b1ce8dbb0054771eba409539df85a963296d2878880710515"
              "3c90fa3ec3d761228e90f8b8"},
      {.input = kManyAs.substr(0, 128),
       .hex = "fc6c71f688f43ea7d60817478808f3cac753e61571865c95adbc2d9122c943a76b92c2cb1047ef3fe7bf6e436ec1d0a99a9e5b21"
              "6780bf7fed9d7ca91d3a8f3b"},
      {.input = kManyAs.substr(0, 129),
       .hex = "55e6e0eb418149a8af92fd9ddc99254781b2f522a131b4f4d984404b71a00e1167b8124d5dcddd4c6977b299392335d6edd303da"
              "6d344d74bbef2d38101b232b"},
  });
  static constexpr std::string_view kMillionAHex =
      "98fb3efb7206fd19ebf69b6f312cf7b64e3b94dbe1a17107913975a793f177e1d077609d7fba363cbba00d05f7aa4e4fa8715d6428104c0a"
      "75643b0ff3fd3eaf";
};

template<>
struct AlgoTraits<blake2b_256::Algorithm> {
  static constexpr std::string_view kName = "blake2b_256";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8"},
      {.input = "abc", .hex = "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319"},
      {.input = kTwoBlockMsg, .hex = "5f7a93da9c5621583f22e49e8e91a40cbba37536622235a380f434b9f68e49c4"},
      {.input = kFourBlockMsg, .hex = "90a0bcf5e5a67ac1578c2754617994cfc248109275a809a0721feebd1e918738"},
      {.input = std::string_view{"\0", 1}, .hex = "03170a2e7597b7b7e3d84c05391d139a62b157e78786d8c082f29dcf4c111314"},
      {.input = kManyAs.substr(0, 127), .hex = "59e2f1aba240f20aa591016f5ef429990bc9c2131dcd0d30f0ffd75ed18f317d"},
      {.input = kManyAs.substr(0, 128), .hex = "ae2aa48507885c4c950fb809b2076f959cde9f8ea6da260d9a3587df33dac450"},
      {.input = kManyAs.substr(0, 129), .hex = "2f64744a6de0d2c0b56e64cf6e29a5aaa255010d415d51c75ccc82f73dccd865"},
  });
  static constexpr std::string_view kMillionAHex = "0741850f36cba4259628355d1073e24ddb9ca0e1bfac36fd39ae5dc2101e23a4";
};

template<>
struct AlgoTraits<blake3::Algorithm> {
  static constexpr std::string_view kName = "blake3::Algorithm";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262"},
      {.input = "abc", .hex = "6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85"},
      {.input = kTwoBlockMsg, .hex = "c19012cc2aaf0dc3d8e5c45a1b79114d2df42abb2a410bf54be09e891af06ff8"},
      {.input = kFourBlockMsg, .hex = "553e1aa2a477cb3166e6ab38c12d59f6c5017f0885aaf079f217da00cfca363f"},
      {.input = std::string_view{"\0", 1}, .hex = "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213"},
      {.input = kManyAs.substr(0, 55), .hex = "8e0494b8aa1fa7fc245b4de5ecfb343f35550e6cc3c051e1e872c4a0a4105f83"},
      {.input = kManyAs.substr(0, 56), .hex = "86dd7cd514f2b1f6aaa34688ead22746f453e9d9ddeeca1ef124477507aefc9f"},
      {.input = kManyAs.substr(0, 63), .hex = "1a2a060cf56e4a859d80723cac9e2391d3c09a33008483e5424c57fe68629b79"},
      {.input = kManyAs.substr(0, 64), .hex = "472c51290d607f100d2036fdcedd7590bba245e9adeb21364a063b7bb4ca81c7"},
      {.input = kManyAs.substr(0, 65), .hex = "f345679d9055e53939e92c04ff4f6c9d824b849810d4b598f54baa23336cde99"},
  });
  static constexpr std::string_view kMillionAHex = "616f575a1b58d4c9797d4217b9730ae5e6eb319d76edef6549b46f4efe31ff8b";
};

template<>
struct AlgoTraits<shake128::Algorithm<kShakeTestOutput>> {
  static constexpr std::string_view kName = "shake128";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26"},
      {.input = "abc", .hex = "5881092dd818bf5cf8a3ddb793fbcba74097d5c526a6d35f97b83351940f2cc8"},
      {.input = kTwoBlockMsg, .hex = "1a96182b50fb8c7e74e0a707788f55e98209b8d91fade8f32f8dd5cff7bf21f5"},
      {.input = kFourBlockMsg, .hex = "7b6df6ff181173b6d7898d7ff63fb07b7c237daf471a5ae5602adbccef9ccf4b"},
      {.input = std::string_view{"\0", 1}, .hex = "0b784469a0628e03861cd8a196dfafa0e9e8056d04cddcc49f0746b9ad43ccb2"},
      {.input = kManyAs.substr(0, 167), .hex = "4f5c6c53ae8190a8ff8a55b2125d28703052d10278570960c2066a905d916c34"},
      {.input = kManyAs.substr(0, 168), .hex = "c22e11586c22b713bde373fce93314d76829de2c21d940a28eb659b8dec953a2"},
      {.input = kManyAs.substr(0, 169), .hex = "09fc23f3acfd944380db0c7f5b1bde62d3a43c6e4c61ca9cb3dfee54904b36a8"},
  });
  static constexpr std::string_view kMillionAHex = "9d222c79c4ff9d092cf6ca86143aa411e369973808ef97093255826c5572ef58";
};

template<>
struct AlgoTraits<shake256::Algorithm<kShakeTestOutput>> {
  static constexpr std::string_view kName = "shake256";
  static constexpr auto kVectors = std::to_array<TestVector>({
      {.input = "", .hex = "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f"},
      {.input = "abc", .hex = "483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739"},
      {.input = kTwoBlockMsg, .hex = "4d8c2dd2435a0128eefbb8c36f6f87133a7911e18d979ee1ae6be5d4fd2e3329"},
      {.input = kFourBlockMsg, .hex = "98be04516c04cc73593fef3ed0352ea9f6443942d6950e29a372a681c3deaf45"},
      {.input = std::string_view{"\0", 1}, .hex = "b8d01df855f7075882c636f6ddeacf41e5de0bbf30042ef0a86e36f4b8600d54"},
      {.input = kManyAs.substr(0, 135), .hex = "55b991ece1e567b6e7c2c714444dd201cd51f4f3832d08e1d26bebc63e07a3d7"},
      {.input = kManyAs.substr(0, 136), .hex = "8fcc5a08f0a1f6827c9cf64ee8d16e0443106359ca6c8efd230759256f44996a"},
      {.input = kManyAs.substr(0, 137), .hex = "a44e1a438dad6273d540be65ee26386c59588efb09139dc086385d2db0c25782"},
  });
  static constexpr std::string_view kMillionAHex = "3578a7a4ca9137569cdf76ed617d31bb994fca9c1bbf8b184013de8234dfd13a";
};

template<typename Algo>
class DigestTest : public ::testing::Test {};

using AllAlgorithms = ::testing::Types<
    sha256::Algorithm,
    sha224::Algorithm,
    sha1::Algorithm,
    sha512::Algorithm,
    sha384::Algorithm,
    sha512_224::Algorithm,
    sha512_256::Algorithm,
    md5::Algorithm,
    sha3_224::Algorithm,
    sha3_256::Algorithm,
    sha3_384::Algorithm,
    sha3_512::Algorithm,
    blake2b::Algorithm,
    blake2b_256::Algorithm,
    blake3::Algorithm,
    shake128::Algorithm<kShakeTestOutput>,
    shake256::Algorithm<kShakeTestOutput>>;
TYPED_TEST_SUITE(DigestTest, AllAlgorithms);

TYPED_TEST(DigestTest, KnownAnswers) {
  using Traits = AlgoTraits<TypeParam>;
  for (const TestVector& vector : Traits::kVectors) {
    EXPECT_THAT(TypeParam::Digest(vector.input), ElementsAreArray(FromHex<TypeParam::kDigestSize>(vector.hex)))
        << Traits::kName << " input of length " << vector.input.size();
    EXPECT_THAT(ToHexString(TypeParam::Digest(vector.input)), vector.hex);
  }
}

TYPED_TEST(DigestTest, ConstexprMatchesRuntime) {
  using Traits = AlgoTraits<TypeParam>;
  // Compile-time proof for one vector per padding case...
  static constexpr auto kCompileTime = TypeParam::Digest(Traits::kVectors.at(1).input);  // "abc"
  static_assert(kCompileTime == FromHex<TypeParam::kDigestSize>(AlgoTraits<TypeParam>::kVectors.at(1).hex));
  // ...and runtime==constexpr equality across all vectors (guards the
  // dual-path `Load32BE`).
  for (const TestVector& vector : Traits::kVectors) {
    const auto runtime = TypeParam::Digest(vector.input);
    EXPECT_THAT(runtime, ElementsAreArray(FromHex<TypeParam::kDigestSize>(vector.hex)));
  }
}

TYPED_TEST(DigestTest, StreamingMatchesOneShot) {
  using Traits = AlgoTraits<TypeParam>;
  std::string all;
  for (const TestVector& vector : Traits::kVectors) {
    all += vector.input;
  }
  const auto expected = TypeParam::Digest(all);
  static constexpr std::array kChunkSizes = std::to_array<std::size_t>({1, 3, 7, 13, 63, 64, 65, 200});
  for (const std::size_t chunk_size : kChunkSizes) {
    Streamer<TypeParam> stream;
    for (std::size_t pos = 0; pos < all.size(); pos += chunk_size) {
      stream.Update(std::string_view(all).substr(pos, chunk_size));
    }
    EXPECT_THAT(stream.Finalize(), ElementsAreArray(expected)) << "chunk size " << chunk_size;
  }
}

TYPED_TEST(DigestTest, StreamerIsPeekable) {
  Streamer<TypeParam> stream;
  stream.Update("abc");
  EXPECT_THAT(stream.Finalize(), ElementsAreArray(TypeParam::Digest("abc")));
  stream.Update("def");
  EXPECT_THAT(stream.Finalize(), ElementsAreArray(TypeParam::Digest("abcdef")));
}

TYPED_TEST(DigestTest, MillionA) {
  using Traits = AlgoTraits<TypeParam>;
  const std::string input(1'000'000, 'a');
  EXPECT_THAT(ToHexString(TypeParam::Digest(input)), Traits::kMillionAHex);
}

struct DigestConvenienceTest : ::testing::Test {};

TEST_F(DigestConvenienceTest, NamespaceFunctionsDelegateToAlgorithms) {
  const std::string input = "abc";
  EXPECT_THAT(sha512::Digest(input), ElementsAreArray(sha512::Algorithm::Digest(input)));
  EXPECT_THAT(sha384::Digest(input), ElementsAreArray(sha384::Algorithm::Digest(input)));
  EXPECT_THAT(sha512_224::Digest(input), ElementsAreArray(sha512_224::Algorithm::Digest(input)));
  EXPECT_THAT(sha512_256::Digest(input), ElementsAreArray(sha512_256::Algorithm::Digest(input)));
  EXPECT_THAT(sha3_224::Digest(input), ElementsAreArray(sha3_224::Algorithm::Digest(input)));
  EXPECT_THAT(sha3_256::Digest(input), ElementsAreArray(sha3_256::Algorithm::Digest(input)));
  EXPECT_THAT(sha3_384::Digest(input), ElementsAreArray(sha3_384::Algorithm::Digest(input)));
  EXPECT_THAT(sha3_512::Digest(input), ElementsAreArray(sha3_512::Algorithm::Digest(input)));
  EXPECT_THAT(blake2b::Digest(input), ElementsAreArray(blake2b::Algorithm::Digest(input)));
  EXPECT_THAT(blake2b_256::Digest(input), ElementsAreArray(blake2b_256::Algorithm::Digest(input)));
}

// BLAKE3 official test-vector suite (BLAKE3-team/BLAKE3
// test_vectors/test_vectors.json): input is the repeating byte pattern
// 0..250; the lengths exercise every tree shape (block/chunk boundaries,
// odd trees, 100 chunks). Checks all three modes per length.
struct Blake3OfficialVector {
  std::size_t input_len;
  std::string_view hash32;
  std::string_view keyed32;
  std::string_view derived32;
};

constexpr std::string_view kBlake3Key = "whats the Elvish word for friend";
constexpr std::string_view kBlake3Context = "BLAKE3 2019-12-27 16:29:52 test vectors context";

constexpr auto kBlake3OfficialVectors = std::to_array<Blake3OfficialVector>({
    {.input_len = 0,
     .hash32 = "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262",
     .keyed32 = "92b2b75604ed3c761f9d6f62392c8a9227ad0ea3f09573e783f1498a4ed60d26",
     .derived32 = "2cc39783c223154fea8dfb7c1b1660f2ac2dcbd1c1de8277b0b0dd39b7e50d7d"},
    {.input_len = 1,
     .hash32 = "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213",
     .keyed32 = "6d7878dfff2f485635d39013278ae14f1454b8c0a3a2d34bc1ab38228a80c95b",
     .derived32 = "b3e2e340a117a499c6cf2398a19ee0d29cca2bb7404c73063382693bf66cb06c"},
    {.input_len = 2,
     .hash32 = "7b7015bb92cf0b318037702a6cdd81dee41224f734684c2c122cd6359cb1ee63",
     .keyed32 = "5392ddae0e0a69d5f40160462cbd9bd889375082ff224ac9c758802b7a6fd20a",
     .derived32 = "1f166565a7df0098ee65922d7fea425fb18b9943f19d6161e2d17939356168e6"},
    {.input_len = 3,
     .hash32 = "e1be4d7a8ab5560aa4199eea339849ba8e293d55ca0a81006726d184519e647f",
     .keyed32 = "39e67b76b5a007d4921969779fe666da67b5213b096084ab674742f0d5ec62b9",
     .derived32 = "440aba35cb006b61fc17c0529255de438efc06a8c9ebf3f2ddac3b5a86705797"},
    {.input_len = 4,
     .hash32 = "f30f5ab28fe047904037f77b6da4fea1e27241c5d132638d8bedce9d40494f32",
     .keyed32 = "7671dde590c95d5ac9616651ff5aa0a27bee5913a348e053b8aa9108917fe070",
     .derived32 = "f46085c8190d69022369ce1a18880e9b369c135eb93f3c63550d3e7630e91060"},
    {.input_len = 5,
     .hash32 = "b40b44dfd97e7a84a996a91af8b85188c66c126940ba7aad2e7ae6b385402aa2",
     .keyed32 = "73ac69eecf286894d8102018a6fc729f4b1f4247d3703f69bdc6a5fe3e0c8461",
     .derived32 = "1f24eda69dbcb752847ec3ebb5dd42836d86e58500c7c98d906ecd82ed9ae47f"},
    {.input_len = 6,
     .hash32 = "06c4e8ffb6872fad96f9aaca5eee1553eb62aed0ad7198cef42e87f6a616c844",
     .keyed32 = "82d3199d0013035682cc7f2a399d4c212544376a839aa863a0f4c91220ca7a6d",
     .derived32 = "be96b30b37919fe4379dfbe752ae77b4f7e2ab92f7ff27435f76f2f065f6a5f4"},
    {.input_len = 7,
     .hash32 = "3f8770f387faad08faa9d8414e9f449ac68e6ff0417f673f602a646a891419fe",
     .keyed32 = "af0a7ec382aedc0cfd626e49e7628bc7a353a4cb108855541a5651bf64fbb28a",
     .derived32 = "dc3b6485f9d94935329442916b0d059685ba815a1fa2a14107217453a7fc9f0e"},
    {.input_len = 8,
     .hash32 = "2351207d04fc16ade43ccab08600939c7c1fa70a5c0aaca76063d04c3228eaeb",
     .keyed32 = "be2f5495c61cba1bb348a34948c004045e3bd4dae8f0fe82bf44d0da245a0600",
     .derived32 = "2b166978cef14d9d438046c720519d8b1cad707e199746f1562d0c87fbd32940"},
    {.input_len = 63,
     .hash32 = "e9bc37a594daad83be9470df7f7b3798297c3d834ce80ba85d6e207627b7db7b",
     .keyed32 = "bb1eb5d4afa793c1ebdd9fb08def6c36d10096986ae0cfe148cd101170ce37ae",
     .derived32 = "b6451e30b953c206e34644c6803724e9d2725e0893039cfc49584f991f451af3"},
    {.input_len = 64,
     .hash32 = "4eed7141ea4a5cd4b788606bd23f46e212af9cacebacdc7d1f4c6dc7f2511b98",
     .keyed32 = "ba8ced36f327700d213f120b1a207a3b8c04330528586f414d09f2f7d9ccb7e6",
     .derived32 = "a5c4a7053fa86b64746d4bb688d06ad1f02a18fce9afd3e818fefaa7126bf73e"},
    {.input_len = 65,
     .hash32 = "de1e5fa0be70df6d2be8fffd0e99ceaa8eb6e8c93a63f2d8d1c30ecb6b263dee",
     .keyed32 = "c0a4edefa2d2accb9277c371ac12fcdbb52988a86edc54f0716e1591b4326e72",
     .derived32 = "51fd05c3c1cfbc8ed67d139ad76f5cf8236cd2acd26627a30c104dfd9d3ff8a8"},
    {.input_len = 127,
     .hash32 = "d81293fda863f008c09e92fc382a81f5a0b4a1251cba1634016a0f86a6bd640d",
     .keyed32 = "c64200ae7dfaf35577ac5a9521c47863fb71514a3bcad18819218b818de85818",
     .derived32 = "c91c090ceee3a3ac81902da31838012625bbcd73fcb92e7d7e56f78deba4f0c3"},
    {.input_len = 128,
     .hash32 = "f17e570564b26578c33bb7f44643f539624b05df1a76c81f30acd548c44b45ef",
     .keyed32 = "b04fe15577457267ff3b6f3c947d93be581e7e3a4b018679125eaf86f6a628ec",
     .derived32 = "81720f34452f58a0120a58b6b4608384b5c51d11f39ce97161a0c0e442ca0225"},
    {.input_len = 129,
     .hash32 = "683aaae9f3c5ba37eaaf072aed0f9e30bac0865137bae68b1fde4ca2aebdcb12",
     .keyed32 = "d4a64dae6cdccbac1e5287f54f17c5f985105457c1a2ec1878ebd4b57e20d38f",
     .derived32 = "938d2d4435be30eafdbb2b7031f7857c98b04881227391dc40db3c7b21f41fc1"},
    {.input_len = 1'023,
     .hash32 = "10108970eeda3eb932baac1428c7a2163b0e924c9a9e25b35bba72b28f70bd11",
     .keyed32 = "c951ecdf03288d0fcc96ee3413563d8a6d3589547f2c2fb36d9786470f1b9d6e",
     .derived32 = "74a16c1c3d44368a86e1ca6df64be6a2f64cce8f09220787450722d85725dea5"},
    {.input_len = 1'024,
     .hash32 = "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af7",
     .keyed32 = "75c46f6f3d9eb4f55ecaaee480db732e6c2105546f1e675003687c31719c7ba4",
     .derived32 = "7356cd7720d5b66b6d0697eb3177d9f8d73a4a5c5e968896eb6a689684302706"},
    {.input_len = 1'025,
     .hash32 = "d00278ae47eb27b34faecf67b4fe263f82d5412916c1ffd97c8cb7fb814b8444",
     .keyed32 = "357dc55de0c7e382c900fd6e320acc04146be01db6a8ce7210b7189bd664ea69",
     .derived32 = "effaa245f065fbf82ac186839a249707c3bddf6d3fdda22d1b95a3c970379bcb"},
    {.input_len = 2'048,
     .hash32 = "e776b6028c7cd22a4d0ba182a8bf62205d2ef576467e838ed6f2529b85fba24a",
     .keyed32 = "879cf1fa2ea0e79126cb1063617a05b6ad9d0b696d0d757cf053439f60a99dd1",
     .derived32 = "7b2945cb4fef70885cc5d78a87bf6f6207dd901ff239201351ffac04e1088a23"},
    {.input_len = 2'049,
     .hash32 = "5f4d72f40d7a5f82b15ca2b2e44b1de3c2ef86c426c95c1af0b6879522563030",
     .keyed32 = "9f29700902f7c86e514ddc4df1e3049f258b2472b6dd5267f61bf13983b78dd5",
     .derived32 = "2ea477c5515cc3dd606512ee72bb3e0e758cfae7232826f35fb98ca1bcbdf273"},
    {.input_len = 3'072,
     .hash32 = "b98cb0ff3623be03326b373de6b9095218513e64f1ee2edd2525c7ad1e5cffd2",
     .keyed32 = "044a0e7b172a312dc02a4c9a818c036ffa2776368d7f528268d2e6b5df191770",
     .derived32 = "050df97f8c2ead654d9bb3ab8c9178edcd902a32f8495949feadcc1e0480c46b"},
    {.input_len = 3'073,
     .hash32 = "7124b49501012f81cc7f11ca069ec9226cecb8a2c850cfe644e327d22d3e1cd3",
     .keyed32 = "68dede9bef00ba89e43f31a6825f4cf433389fedae75c04ee9f0cf16a427c95a",
     .derived32 = "72613c9ec9ff7e40f8f5c173784c532ad852e827dba2bf85b2ab4b76f7079081"},
    {.input_len = 4'096,
     .hash32 = "015094013f57a5277b59d8475c0501042c0b642e531b0a1c8f58d2163229e969",
     .keyed32 = "befc660aea2f1718884cd8deb9902811d332f4fc4a38cf7c7300d597a081bfc0",
     .derived32 = "1e0d7f3db8c414c97c6307cbda6cd27ac3b030949da8e23be1a1a924ad2f25b9"},
    {.input_len = 4'097,
     .hash32 = "9b4052b38f1c5fc8b1f9ff7ac7b27cd242487b3d890d15c96a1c25b8aa0fb995",
     .keyed32 = "00df940cd36bb9fa7cbbc3556744e0dbc8191401afe70520ba292ee3ca80abbc",
     .derived32 = "aca51029626b55fda7117b42a7c211f8c6e9ba4fe5b7a8ca922f34299500ead8"},
    {.input_len = 5'120,
     .hash32 = "9cadc15fed8b5d854562b26a9536d9707cadeda9b143978f319ab34230535833",
     .keyed32 = "2c493e48e9b9bf31e0553a22b23503c0a3388f035cece68eb438d22fa1943e20",
     .derived32 = "7a7acac8a02adcf3038d74cdd1d34527de8a0fcc0ee3399d1262397ce5817f60"},
    {.input_len = 5'121,
     .hash32 = "628bd2cb2004694adaab7bbd778a25df25c47b9d4155a55f8fbd79f2fe154cff",
     .keyed32 = "6ccf1c34753e7a044db80798ecd0782a8f76f33563accaddbfbb2e0ea4b2d024",
     .derived32 = "b07f01e518e702f7ccb44a267e9e112d403a7b3f4883a47ffbed4b48339b3c34"},
    {.input_len = 6'144,
     .hash32 = "3e2e5b74e048f3add6d21faab3f83aa44d3b2278afb83b80b3c35164ebeca205",
     .keyed32 = "3d6b6d21281d0ade5b2b016ae4034c5dec10ca7e475f90f76eac7138e9bc8f1d",
     .derived32 = "2a95beae63ddce523762355cf4b9c1d8f131465780a391286a5d01abb5683a15"},
    {.input_len = 6'145,
     .hash32 = "f1323a8631446cc50536a9f705ee5cb619424d46887f3c376c695b70e0f0507f",
     .keyed32 = "9ac301e9e39e45e3250a7e3b3df701aa0fb6889fbd80eeecf28dbc6300fbc539",
     .derived32 = "379bcc61d0051dd489f686c13de00d5b14c505245103dc040d9e4dd1facab8e5"},
    {.input_len = 7'168,
     .hash32 = "61da957ec2499a95d6b8023e2b0e604ec7f6b50e80a9678b89d2628e99ada77a",
     .keyed32 = "b42835e40e9d4a7f42ad8cc04f85a963a76e18198377ed84adddeaecacc6f3fc",
     .derived32 = "11c37a112765370c94a51415d0d651190c288566e295d505defdad895dae2237"},
    {.input_len = 7'169,
     .hash32 = "a003fc7a51754a9b3c7fae0367ab3d782dccf28855a03d435f8cfe74605e7817",
     .keyed32 = "ed9b1a922c046fdb3d423ae34e143b05ca1bf28b710432857bf738bcedbfa511",
     .derived32 = "554b0a5efea9ef183f2f9b931b7497995d9eb26f5c5c6dad2b97d62fc5ac31d9"},
    {.input_len = 8'192,
     .hash32 = "aae792484c8efe4f19e2ca7d371d8c467ffb10748d8a5a1ae579948f718a2a63",
     .keyed32 = "dc9637c8845a770b4cbf76b8daec0eebf7dc2eac11498517f08d44c8fc00d58a",
     .derived32 = "ad01d7ae4ad059b0d33baa3c01319dcf8088094d0359e5fd45d6aeaa8b2d0c3d"},
    {.input_len = 8'193,
     .hash32 = "bab6c09cb8ce8cf459261398d2e7aef35700bf488116ceb94a36d0f5f1b7bc3b",
     .keyed32 = "954a2a75420c8d6547e3ba5b98d963e6fa6491addc8c023189cc519821b4a1f5",
     .derived32 = "af1e0346e389b17c23200270a64aa4e1ead98c61695d917de7d5b00491c9b0f1"},
    {.input_len = 16'384,
     .hash32 = "f875d6646de28985646f34ee13be9a576fd515f76b5b0a26bb324735041ddde4",
     .keyed32 = "9e9fc4eb7cf081ea7c47d1807790ed211bfec56aa25bb7037784c13c4b707b0d",
     .derived32 = "160e18b5878cd0df1c3af85eb25a0db5344d43a6fbd7a8ef4ed98d0714c3f7e1"},
    {.input_len = 31'744,
     .hash32 = "62b6960e1a44bcc1eb1a611a8d6235b6b4b78f32e7abc4fb4c6cdcce94895c47",
     .keyed32 = "efa53b389ab67c593dba624d898d0f7353ab99e4ac9d42302ee64cbf9939a419",
     .derived32 = "39772aef80e0ebe60596361e45b061e8f417429d529171b6764468c22928e28e"},
    {.input_len = 102'400,
     .hash32 = "bc3e3d41a1146b069abffad3c0d44860cf664390afce4d9661f7902e7943e085",
     .keyed32 = "1c35d1a5811083fd7119f5d5d1ba027b4d01c0c6c49fb6ff2cf75393ea5db4a7",
     .derived32 = "4652cff7a3f385a6103b5c260fc1593e13c778dbe608efb092fe7ee69df6e9c6"},
});

std::string Blake3PatternInput(std::size_t len) {
  std::string input(len, '\0');
  for (std::size_t i = 0; i < len; ++i) {
    constexpr std::size_t kPatternPeriod = 251;  // The official vectors' repeating byte pattern.
    input.at(i) = static_cast<char>(i % kPatternPeriod);
  }
  return input;
}

TEST(Blake3Test, OfficialVectorsAllModes) {
  for (const Blake3OfficialVector& vector : kBlake3OfficialVectors) {
    const std::string input = Blake3PatternInput(vector.input_len);
    EXPECT_THAT(ToHexString(blake3::Digest(input)), vector.hash32) << "len " << vector.input_len;
    EXPECT_THAT(ToHexString(blake3::DigestKeyed(kBlake3Key, input)), vector.keyed32) << "len " << vector.input_len;
    EXPECT_THAT(ToHexString(blake3::DeriveKey(kBlake3Context, input)), vector.derived32) << "len " << vector.input_len;
  }
}

// The official vectors publish 131 bytes of output - more than two output
// blocks, proving the XOF squeeze (root counter increments).
struct Blake3XofVector {
  std::size_t input_len;
  std::string_view hex;
};

constexpr auto kBlake3XofVectors = std::to_array<Blake3XofVector>({
    {.input_len = 0,
     .hex = "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262e00f03e7b69af26b7faaf09fcd333050338ddfe085"
            "b8cc869ca98b206c08243a26f5487789e8f660afe6c99ef9e0c52b92e7393024a80459cf91f476f9ffdbda7001c22e159b402631f2"
            "77ca96f2defdf1078282314e763699a31c5363165421cce14d"},
    {.input_len = 1'024,
     .hex = "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af71cf8107265ecdaf8505b95d8fcec83a98a6a96ea51"
            "09d2c179c47a387ffbb404756f6eeae7883b446b70ebb144527c2075ab8ab204c0086bb22b7c93d465efc57f8d917f0b385c6df265"
            "e77003b85102967486ed57db5c5ca170ba441427ed9afa684e"},
    {.input_len = 102'400,
     .hex = "bc3e3d41a1146b069abffad3c0d44860cf664390afce4d9661f7902e7943e085e01c59dab908c04c3342b816941a26d69c2605ebee"
            "5ec5291cc55e15b76146e6745f0601156c3596cb75065a9c57f35585a52e1ac70f69131c23d611ce11ee4ab1ec2c009012d236648e"
            "77be9295dd0426f29b764d65de58eb7d01dd42248204f45f8e"},
});

TEST(Blake3Test, OfficialVectorsXof) {
  for (const Blake3XofVector& vector : kBlake3XofVectors) {
    EXPECT_THAT(ToHexString(blake3::DigestXof<131>(Blake3PatternInput(vector.input_len))), vector.hex)
        << "len " << vector.input_len;
  }
}

TEST(Blake3Test, KeyedStreamingMatchesOneShot) {
  constexpr std::size_t kChunkStep = 100;
  const std::string input = Blake3PatternInput(3'073);
  auto state = blake3::Algorithm::StreamInitKeyed(kBlake3Key);
  for (std::size_t pos = 0; pos < input.size(); pos += kChunkStep) {
    blake3::Algorithm::StreamUpdate(state, std::string_view(input).substr(pos, kChunkStep));
  }
  EXPECT_THAT(blake3::Algorithm::StreamFinalize(state), ElementsAreArray(blake3::DigestKeyed(kBlake3Key, input)));
}

// Compile-time proof for all three BLAKE3 modes (first official vector rows).
static_assert(blake3::Digest("") == FromHex<blake3::kDigestSize>(kBlake3OfficialVectors.at(0).hash32));
static_assert(
    blake3::DigestKeyed(kBlake3Key, "") == FromHex<blake3::kDigestSize>(kBlake3OfficialVectors.at(0).keyed32));
static_assert(
    blake3::DeriveKey(kBlake3Context, "") == FromHex<blake3::kDigestSize>(kBlake3OfficialVectors.at(0).derived32));

// SHAKE outputs longer than the rate need squeeze-block iteration; 200 bytes
// crosses it for both rates (168/136). Values from python hashlib.
TEST(ShakeTest, LongOutputCrossesRate) {
  EXPECT_THAT(
      ToHexString(shake128::Digest<200>("")),
      "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef263cb1eea988004b93103cfb0aeefd2a686e01fa4a58e8a363"
      "9ca8a1e3f9ae57e235b8cc873c23dc62b8d260169afa2f75ab916a58d974918835d25e6a435085b2badfd6dfaac359a5efbb7bcc4b59d538"
      "df9a04302e10c8bc1cbf1a0b3a5120ea17cda7cfad765f5623474d368ccca8af0007cd9f5e4c849f167a580b14aabdefaee7eef47cb0fca9"
      "767be1fda69419dfb927e9df07348b196691abaeb580b32def58538b8d23f877");
  EXPECT_THAT(
      ToHexString(shake256::Digest<200>("abc")),
      "483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739d5a15bef186a5386c75744c0527e1faa9f8726e462a12a4f"
      "eb06bd8801e751e41385141204f329979fd3047a13c5657724ada64d2470157b3cdc288620944d78dbcddbd912993f0913f164fb2ce95131"
      "a2d09a3e6d51cbfc622720d7a75c6334e8a2d7ec71a7cc29cf0ea610eeff1a588290a53000faa79932becec0bd3cd0b33a7e5d397fed1ada"
      "9442b99903f4dcfd8559ed3950faf40fe6f3b5d710ed3b677513771af6bfe119");
}

// Requesting fewer bytes yields a prefix of the longer output (XOF property).
TEST(ShakeTest, OutputsSharePrefix) {
  const auto short_out = shake256::Digest<16>("abc");
  const auto long_out = shake256::Digest<64>("abc");
  EXPECT_TRUE(std::equal(short_out.begin(), short_out.end(), long_out.begin()));
}

// BLAKE2b native keyed mode (RFC 7693 key block; values from python hashlib).
struct Blake2bKeyedVector {
  std::size_t digest_size;
  std::string_view key;
  std::string_view message;
  std::string_view hex;
};

constexpr auto kBlake2bKeyedVectors = std::to_array<Blake2bKeyedVector>({
    {.digest_size = 64,
     .key = "key",
     .message = "",
     .hex = "5b3cfd8f422b490b764b55eceb330b500c79cbefa9a928ad00202b8b3c5dd778a81122570434a2e3b8bfd028d105dfefd0a9576e88"
            "ed66de742ca9fbb5f8d2b6"},
    {.digest_size = 64,
     .key = "key",
     .message = "abc",
     .hex = "5c6a9a4ae911c02fb7e71a991eb9aea371ae993d4842d206e6020d46f5e41358c6d5c277c110ef86c959ed63e6ecaaaceaaff38019"
            "a43264ae06acf73b9550b1"},
    {.digest_size = 64,
     .key = kManyKs.substr(0, 32),
     .message = kFoxMsg,
     .hex = "307f718e2e4c9b57def1976d5818b34bc5b0514911524f481d237bffa2256c5b471eca9ee29beae6aff629b764ebcbc7975df8c609"
            "36130888a2c16a48239081"},
    {.digest_size = 64,
     .key = kManyKs.substr(0, 64),
     .message = kManyAs.substr(0, 129),
     .hex = "8d7d13680af8a8613d211145a1b2d1c3d3804ac67fe91829082f897c211945dae4414c66f8e0e95465226fb58d81303cbda985e832"
            "c90352e2c8c2f9bd28800e"},
    {.digest_size = 64,
     .key = kManyKs.substr(0, 64),
     .message = "",
     .hex = "5caefa9818315414d0d763300fc08c4c7c434a259b75c5ed814e97c313ab1b157276e5456967794c810cdbd0b5c5266503067a4a49"
            "9d29615693e063b1b6afb9"},
    {.digest_size = 32,
     .key = "key",
     .message = "abc",
     .hex = "0330531d097355a3f72e80d55c1245ccf79f1704431c6e3887938320442c23c0"},
});

TEST(Blake2bTest, KeyedKnownAnswers) {
  for (const Blake2bKeyedVector& vector : kBlake2bKeyedVectors) {
    const std::string hex = vector.digest_size == 64
                                ? ToHexString(blake2b::Algorithm::DigestKeyed(vector.key, vector.message))
                                : ToHexString(blake2b_256::Algorithm::DigestKeyed(vector.key, vector.message));
    EXPECT_THAT(hex, vector.hex) << "key length " << vector.key.size() << ", message length " << vector.message.size();
  }
}

TEST(Blake2bTest, KeyedStreamingMatchesOneShot) {
  const Blake2bKeyedVector& vector = kBlake2bKeyedVectors.at(3);  // 64-byte key, 129-byte message.
  auto state = blake2b::Algorithm::StreamInitKeyed(vector.key);
  for (const char& chr : vector.message) {
    blake2b::Algorithm::StreamUpdate(state, std::string_view(&chr, 1));
  }
  EXPECT_THAT(ToHexString(blake2b::Algorithm::StreamFinalize(state)), vector.hex);
}

// HMAC known answers (generated with python's hmac module) covering short,
// empty, exactly-block-sized, and longer-than-block keys.
struct HmacVector {
  std::string_view key;
  std::string_view message;
  std::string_view hex;
};

template<typename Algo>
struct HmacTraits;

template<>
struct HmacTraits<md5::Algorithm> {
  static constexpr auto kVectors = std::to_array<HmacVector>({
      {.key = "key", .message = kFoxMsg, .hex = "80070713463e7749b90c2dc24911e275"},
      {.key = "", .message = "", .hex = "74e6f7298a9c2d168935f58c001bad88"},
      {.key = kManyKs.substr(0, 64), .message = "abc", .hex = "0be890bbca0302e362a6c689fc3debcb"},
      {.key = kManyKs.substr(0, 101), .message = kFoxMsg, .hex = "ef4a1032746d820207cadec8410a3cd0"},
  });
};

template<>
struct HmacTraits<sha1::Algorithm> {
  static constexpr auto kVectors = std::to_array<HmacVector>({
      {.key = "key", .message = kFoxMsg, .hex = "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9"},
      {.key = "", .message = "", .hex = "fbdb1d1b18aa6c08324b7d64b71fb76370690e1d"},
      {.key = kManyKs.substr(0, 64), .message = "abc", .hex = "7c44f6972fe89fcc6df413921b6e3616adffa964"},
      {.key = kManyKs.substr(0, 101), .message = kFoxMsg, .hex = "f1ceec83638f69d7fe92200114bb7a01281e8f85"},
  });
};

template<>
struct HmacTraits<sha256::Algorithm> {
  static constexpr auto kVectors = std::to_array<HmacVector>({
      {.key = "key", .message = kFoxMsg, .hex = "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"},
      {.key = "", .message = "", .hex = "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad"},
      {.key = kManyKs.substr(0, 64),
       .message = "abc",
       .hex = "ae0c0e4a2340cf50185eb46aaa8723f4769153661612e212fb0d1fa3170c6202"},
      {.key = kManyKs.substr(0, 101),
       .message = kFoxMsg,
       .hex = "6db9aaee699c68867a3485a31581ae5c6e07c93dab89a6e7d63cc9e6782125ae"},
  });
};

template<>
struct HmacTraits<sha512::Algorithm> {
  static constexpr auto kVectors = std::to_array<HmacVector>({
      {.key = "key",
       .message = kFoxMsg,
       .hex = "b42af09057bac1e2d41708e48a902e09b5ff7f12ab428a4fe86653c73dd248fb82f948a549f7b791a5b41915ee4d1ec3935357e4"
              "e2317250d0372afa2ebeeb3a"},
      {.key = "",
       .message = "",
       .hex = "b936cee86c9f87aa5d3c6f2e84cb5a4239a5fe50480a6ec66b70ab5b1f4ac6730c6c515421b327ec1d69402e53dfb49ad7381eb0"
              "67b338fd7b0cb22247225d47"},
      {.key = kManyKs.substr(0, 128),
       .message = "abc",
       .hex = "b3327a783059e7fa5af7b4836f7b7d3a9403e96a2cb5fd4dd50a78b3eb9b4e71111fc2286d58ea8f997d4271e709ae184d7d825c"
              "bab47c392970b4d84190b9c3"},
      {.key = kManyKs.substr(0, 165),
       .message = kFoxMsg,
       .hex = "ce0ed52d7b91569f67f074974d1295d0a639df4c37962a27be14eb236e9dee327ab8c9535ed4c715f3f961a5148a3297d8c8b7ea"
              "e7529aae1f05e0931bda1e27"},
  });
};

template<>
struct HmacTraits<sha3_256::Algorithm> {
  static constexpr auto kVectors = std::to_array<HmacVector>({
      {.key = "key", .message = kFoxMsg, .hex = "8c6e0683409427f8931711b10ca92a506eb1fafa48fadd66d76126f47ac2c333"},
      {.key = "", .message = "", .hex = "e841c164e5b4f10c9f3985587962af72fd607a951196fc92fb3a5251941784ea"},
      {.key = kManyKs.substr(0, 136),
       .message = "abc",
       .hex = "ac426c65e5140ac0a28def30012fc621fae635683ff5bb254d58f6f3a73e8830"},
      {.key = kManyKs.substr(0, 173),
       .message = kFoxMsg,
       .hex = "815cf700506a8751b80b40e1fd2f95550f6ee8263b0d6032e2d9c08f6c548635"},
  });
};

// Compile-time proof that HMAC is constexpr end to end.
static_assert(
    Hmac<sha256::Algorithm>::Digest(
        HmacTraits<sha256::Algorithm>::kVectors.at(0).key,
        HmacTraits<sha256::Algorithm>::kVectors.at(0).message)
    == FromHex<sha256::kDigestSize>(HmacTraits<sha256::Algorithm>::kVectors.at(0).hex));

template<typename Algo>
class HmacTest : public ::testing::Test {};

using HmacAlgorithms =
    ::testing::Types<md5::Algorithm, sha1::Algorithm, sha256::Algorithm, sha512::Algorithm, sha3_256::Algorithm>;
TYPED_TEST_SUITE(HmacTest, HmacAlgorithms);

TYPED_TEST(HmacTest, KnownAnswers) {
  for (const HmacVector& vector : HmacTraits<TypeParam>::kVectors) {
    EXPECT_THAT(ToHexString(Hmac<TypeParam>::Digest(vector.key, vector.message)), vector.hex)
        << "key length " << vector.key.size() << ", message length " << vector.message.size();
  }
}

TYPED_TEST(HmacTest, StreamingMatchesOneShotAndPeeks) {
  const HmacVector& vector = HmacTraits<TypeParam>::kVectors.at(0);
  HmacStreamer<TypeParam> stream(vector.key);
  for (const char& chr : vector.message) {
    stream.Update(std::string_view(&chr, 1));
  }
  EXPECT_THAT(ToHexString(stream.Finalize()), vector.hex);

  constexpr std::size_t kSplit = 10;
  HmacStreamer<TypeParam> peeking(vector.key);
  peeking.Update(vector.message.substr(0, kSplit));
  (void)peeking.Finalize();  // Peek must not disturb the stream.
  peeking.Update(vector.message.substr(kSplit));
  EXPECT_THAT(ToHexString(peeking.Finalize()), vector.hex);
}

TEST(DigestTest, ToHexString) {
  constexpr std::array<uint8_t, 4> kBytes = {0x00, 0x0F, 0xA5, 0xFF};
  EXPECT_THAT(ToHexString(kBytes), "000fa5ff");
}

}  // namespace
}  // namespace mbo::digest
