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

#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <istream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/log/initialize.h"
#include "mbo/digest/checksum.h"
#include "mbo/digest/digest.h"
#include "mbo/strings/indent.h"

// NOLINTBEGIN(*avoid-non-const-global-variables,*abseil-no-namespace)

ABSL_FLAG(  //
    std::string,
    algorithm,
    "sha256",
    R"(Digest algorithm:
- md5, sha1:                                       legacy interop only; COLLISION-BROKEN.
- sha224, sha256, sha384, sha512:                  SHA-2 (FIPS 180-4).
- sha512-224, sha512-256:                          SHA-512/t (FIPS 180-4).
- sha3-224, sha3-256, sha3-384, sha3-512:          SHA-3 (FIPS 202).
- shake128, shake256:                              FIPS 202 XOFs, printed at 32 bytes.
- blake2b, blake2b-256:                            BLAKE2b (RFC 7693), 64/32 bytes.
- blake3:                                          BLAKE3, 32 bytes.
)");
ABSL_FLAG(  //
    std::string,
    a,
    "",
    "Short alias for --algorithm (like `shasum -a`); takes precedence when set.");
ABSL_FLAG(  //
    bool,
    ignore_directories,
    false,
    "Silently skip directory arguments instead of reporting them as errors.");
ABSL_FLAG(  //
    bool,
    d,
    false,
    "Short alias for --ignore_directories.");
ABSL_FLAG(  //
    bool,
    reverse,
    false,
    "Print '<file>  <hash>' instead of the checksum-style '<hash>  <file>'.");
ABSL_FLAG(  //
    bool,
    check,
    false,
    "Verify instead of compute: the file arguments are checksum files ('<hash>  <file>' lines as produced by this "
    "tool or by sha256sum/shasum; '*' binary markers and uppercase hex are accepted). Each listed file is "
    "re-digested with the selected --algorithm and reported as OK / FAILED. See also --quiet, --status, "
    "--ignore_missing, and --strict.");
ABSL_FLAG(  //
    bool,
    c,
    false,
    "Short alias for --check.");
ABSL_FLAG(  //
    std::string,
    cwd,
    "",
    "Change working directory if not empty.");
ABSL_FLAG(  //
    bool,
    quiet,
    false,
    "With --check: do not print OK for each successfully verified file.");
ABSL_FLAG(  //
    bool,
    status,
    false,
    "With --check: print nothing; the exit code carries the result.");
ABSL_FLAG(  //
    bool,
    ignore_missing,
    false,
    "With --check: silently skip listed files that cannot be read (still fails if no file was verified).");
ABSL_FLAG(  //
    bool,
    strict,
    false,
    "With --check: exit non-zero for improperly formatted checksum lines.");

// NOLINTEND(*avoid-non-const-global-variables,*abseil-no-namespace)

namespace mbo::digest {
namespace {

namespace fs = std::filesystem;

// Digests a stream in chunks (no whole-file buffering).
template<typename Algo>
std::string HexDigestStream(std::istream& input) {
  static constexpr std::size_t kChunkSize = 1ULL << 16U;
  Streamer<Algo> stream;
  std::array<char, kChunkSize> buffer = {};
  while (input.read(buffer.data(), kChunkSize).gcount() > 0) {
    stream.Update(std::string_view(buffer.data(), static_cast<std::size_t>(input.gcount())));
  }
  return ToHexString(stream.Finalize());
}

// The SHAKE XOFs have no inherent output size; the CLI prints 32 bytes.
constexpr std::size_t kShakeOutputSize = 32;

using DigestFunc = std::string (*)(std::istream&);

struct NamedAlgorithm {
  std::string_view name;
  DigestFunc func;
  std::size_t hex_length;  // Expected hex-digest length for --check parsing.
};

template<typename Algo>
constexpr NamedAlgorithm Entry(std::string_view name) {
  return {.name = name, .func = &HexDigestStream<Algo>, .hex_length = 2 * Algo::kDigestSize};
}

constexpr auto kAlgorithms = std::to_array<NamedAlgorithm>({
    Entry<md5::Algorithm>("md5"),
    Entry<sha1::Algorithm>("sha1"),
    Entry<sha224::Algorithm>("sha224"),
    Entry<sha256::Algorithm>("sha256"),
    Entry<sha384::Algorithm>("sha384"),
    Entry<sha512::Algorithm>("sha512"),
    Entry<sha512_224::Algorithm>("sha512-224"),
    Entry<sha512_256::Algorithm>("sha512-256"),
    Entry<sha3_224::Algorithm>("sha3-224"),
    Entry<sha3_256::Algorithm>("sha3-256"),
    Entry<sha3_384::Algorithm>("sha3-384"),
    Entry<sha3_512::Algorithm>("sha3-512"),
    Entry<shake128::Algorithm<kShakeOutputSize>>("shake128"),
    Entry<shake256::Algorithm<kShakeOutputSize>>("shake256"),
    Entry<blake2b::Algorithm>("blake2b"),
    Entry<blake2b_256::Algorithm>("blake2b-256"),
    Entry<blake3::Algorithm>("blake3"),
});

const NamedAlgorithm* FindAlgorithm(std::string_view name) {
  for (const NamedAlgorithm& algorithm : kAlgorithms) {
    if (algorithm.name == name) {
      return &algorithm;
    }
  }
  return nullptr;
}

void Print(std::string_view hex, std::string_view file_name) {
  if (absl::GetFlag(FLAGS_reverse)) {
    std::cout << file_name << "  " << hex << "\n";
  } else {
    std::cout << hex << "  " << file_name << "\n";
  }
}

std::string ToLowerHex(std::string_view hex) {
  std::string lower(hex);
  for (char& chr : lower) {
    chr = static_cast<char>(std::tolower(static_cast<unsigned char>(chr)));
  }
  return lower;
}

struct CheckStats {
  std::size_t verified = 0;
  std::size_t mismatched = 0;
  std::size_t unreadable = 0;
  std::size_t malformed = 0;
};

// Verifies all lines of one checksum stream (see the --check flag).
void CheckStream(const NamedAlgorithm& algorithm, std::istream& sums, CheckStats& stats) {
  const bool quiet = absl::GetFlag(FLAGS_quiet) || absl::GetFlag(FLAGS_status);
  const bool silent = absl::GetFlag(FLAGS_status);
  const bool ignore_missing = absl::GetFlag(FLAGS_ignore_missing);
  std::string line;
  while (std::getline(sums, line)) {
    if (line.empty()) {
      continue;
    }
    const std::optional<internal::ChecksumLine> parsed = internal::ParseChecksumLine(line, algorithm.hex_length);
    if (!parsed.has_value()) {
      ++stats.malformed;
      continue;
    }
    const auto [hex, file_name] = *parsed;
    std::ifstream input{fs::path(file_name), std::ios::binary};
    if (!input) {
      if (!ignore_missing) {
        ++stats.unreadable;
        if (!silent) {
          std::cout << file_name << ": FAILED open or read\n";
        }
      }
      continue;
    }
    if (algorithm.func(input) == ToLowerHex(hex)) {
      ++stats.verified;
      if (!quiet) {
        std::cout << file_name << ": OK\n";
      }
    } else {
      ++stats.mismatched;
      if (!silent) {
        std::cout << file_name << ": FAILED\n";
      }
    }
  }
}

// Verifies all checksum files ('-' = stdin); returns the process exit code.
int RunCheck(const NamedAlgorithm& algorithm, const std::vector<std::string_view>& files) {
  CheckStats stats;
  for (const std::string_view file_name : files) {
    if (file_name == "-") {
      CheckStream(algorithm, std::cin, stats);
      continue;
    }
    std::ifstream sums{fs::path(file_name), std::ios::binary};
    if (!sums) {
      std::cerr << file_name << ": Cannot read checksum file.\n";
      return 1;
    }
    CheckStream(algorithm, sums, stats);
  }
  const bool silent = absl::GetFlag(FLAGS_status);
  const auto warn = [&](std::size_t count, std::string_view what) {
    if (count > 0 && !silent) {
      std::cerr << "digest: WARNING: " << count << " " << what << "\n";
    }
  };
  warn(stats.malformed, "line(s) are improperly formatted");
  warn(stats.unreadable, "listed file(s) could not be read");
  warn(stats.mismatched, "computed checksum(s) did NOT match");
  if (absl::GetFlag(FLAGS_ignore_missing) && stats.verified == 0 && stats.mismatched == 0) {
    if (!silent) {
      std::cerr << "digest: no file was verified\n";
    }
    return 1;
  }
  const bool strict_failure = absl::GetFlag(FLAGS_strict) && stats.malformed > 0;
  return (stats.mismatched > 0 || stats.unreadable > 0 || strict_failure) ? 1 : 0;
}

// Digests all `files` ('-' = stdin); returns the process exit code. Errors
// are reported per file and processing continues (like the coreutils tools).
int Run(DigestFunc digest, const std::vector<std::string_view>& files) {
  int result = 0;
  for (const std::string_view file_name : files) {
    if (file_name == "-") {
      Print(digest(std::cin), file_name);
      continue;
    }
    const fs::path path(file_name);
    std::error_code error;
    if (fs::is_directory(path, error)) {
      if (!absl::GetFlag(FLAGS_ignore_directories) && !absl::GetFlag(FLAGS_d)) {
        std::cerr << file_name << ": Is a directory (not supported).\n";
        result = 1;
      }
      continue;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      std::cerr << file_name << ": Cannot read file.\n";
      result = 1;
      continue;
    }
    Print(digest(input), file_name);
  }
  return result;
}

}  // namespace
}  // namespace mbo::digest

int main(int argc, char* argv[]) {
  absl::SetProgramUsageMessage(mbo::strings::DropIndent(R"(
    [ <flags> ] <file>...

    Prints one '<hash>  <file>' line per file (matching the sha256sum/shasum
    output format; '--reverse' swaps the columns). The algorithm is selected
    with '--algorithm' or its short alias '-a' (default sha256). '-' reads
    standard input. Directories are errors unless '--ignore_directories'
    (short: '-d') silently skips them.

    With '--check' (short: '-c') the arguments are checksum files instead and
    every listed file is verified ('OK' / 'FAILED'); the format matches
    sha256sum/shasum, so sum files are interchangeable in both directions.
  )"));
  absl::InitializeLog();
  const std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  std::string algorithm_name = absl::GetFlag(FLAGS_a);
  if (algorithm_name.empty()) {
    algorithm_name = absl::GetFlag(FLAGS_algorithm);
  }
  const mbo::digest::NamedAlgorithm* algorithm = mbo::digest::FindAlgorithm(algorithm_name);
  if (algorithm == nullptr) {
    std::cerr << "Unknown --algorithm '" << algorithm_name
              << "'. Use: " << std::filesystem::path(args.front()).filename().string() << " --help\n";
    return 1;
  }
  if (args.size() < 2) {  // [0] = program
    std::cerr << "At least one file (or '-' for stdin) is required. Use: "
              << std::filesystem::path(args.front()).filename().string() << " --help\n";
    return 1;
  }
  if (!absl::GetFlag(FLAGS_cwd).empty()) {
    std::error_code error;
    std::filesystem::current_path(absl::GetFlag(FLAGS_cwd), error);
    if (error) {
      std::cerr << "Cannot change working directory to '" << absl::GetFlag(FLAGS_cwd) << "': " << error.message()
                << "\n";
      return 1;
    }
  }
  const std::vector<std::string_view> files(args.begin() + 1, args.end());
  if (absl::GetFlag(FLAGS_check) || absl::GetFlag(FLAGS_c)) {
    return mbo::digest::RunCheck(*algorithm, files);
  }
  return mbo::digest::Run(algorithm->func, files);
}
