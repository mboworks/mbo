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

#include "mbo/types/internal/struct_names.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/types/tstring.h"

namespace mbo::types::types_internal {
namespace {

using ::testing::ElementsAre;  // NOLINT(misc-unused-using-decls)

struct StructNamesTest : ::testing::Test {};

template<typename T, std::size_t N, std::array<const char*, N> Names, typename Indices>
struct GetFieldNamesAreImpl;

template<typename T, std::size_t N, std::array<const char*, N> Names, std::size_t... Indices>
struct GetFieldNamesAreImpl<T, N, Names, std::index_sequence<Indices...>>
    : std::bool_constant<((GetFieldNames<T>().at(Indices) == Names.at(Indices)) && ...)> {};

template<typename T, tstring... Names>
concept TestGetFieldNames = GetFieldNamesAreImpl<
    T,
    sizeof...(Names),
    std::array<const char*, sizeof...(Names)>{Names.begin()...},
    std::make_index_sequence<sizeof...(Names)>>::value;

#if (defined(__clang__) && __has_builtin(__builtin_dump_struct)) || (defined(__GNUC__) && !defined(__clang__))

static_assert(kStructNameSupport);

static_assert(!SupportsFieldNames<int>);
static_assert(GetFieldNames<int>().empty());

struct Empty {};

static_assert(SupportsFieldNames<Empty>);
static_assert(GetFieldNames<Empty>().empty());

struct Two {
  int first;
  Empty second;
};

static_assert(SupportsFieldNames<Two>);
static_assert(TestGetFieldNames<Two, "first"_ts, "second"_ts>);

struct WithArray {
  int values[2];  // NOLINT(*-avoid-c-arrays): Required to exercise aggregate field-name extraction.
  int tail;

  [[maybe_unused]] friend consteval std::size_t MboTypesDecomposeCount(const WithArray* /*unused*/) { return 2; }
};

static_assert(SupportsFieldNames<WithArray>);
static_assert(TestGetFieldNames<WithArray, "values"_ts, "tail"_ts>);

struct DefaultConstructorDeleted {
  DefaultConstructorDeleted() = delete;
};

static_assert(!std::is_default_constructible_v<DefaultConstructorDeleted>);
static_assert(SupportsFieldNames<DefaultConstructorDeleted>);

# ifndef IS_CLANGD
// The indexer takes aim at how field names are `looked up` from uninitialized
// memory and reliably crashes here. So this piece is disabled for the indexer.

struct NoDefaultConstructor {
  int& ref;
  int val;
};

static_assert(!std::is_default_constructible_v<NoDefaultConstructor>);
#  if defined(__clang__)
static_assert(SupportsFieldNames<NoDefaultConstructor>);

TEST_F(StructNamesTest, StructWithoutDefaultConstructor) {
  // This cannot be done at compile time.
  EXPECT_THAT(GetFieldNames<NoDefaultConstructor>(), ElementsAre("ref", "val"));
}
#  else
static_assert(!SupportsFieldNames<NoDefaultConstructor>);
#  endif  // defined(__clang__)
# endif   // IS_CLANGD

struct NoDestructor {  // NOLINT(*-special-member-functions)
  constexpr NoDestructor() = default;
  ~NoDestructor() = delete;
};

# if defined(__clang__)
static_assert(!SupportsFieldNames<NoDestructor>);
# else
static_assert(SupportsFieldNames<NoDestructor>);
static_assert(GetFieldNames<NoDestructor>().empty());
# endif  // defined(__clang__)

struct NonTrivialConstexprDtor {  // NOLINT(*-special-member-functions)

  constexpr ~NonTrivialConstexprDtor() { ++field; }

  int field{0};
};

static_assert(std::is_destructible_v<const NonTrivialConstexprDtor>);
static_assert(std::is_aggregate_v<NonTrivialConstexprDtor>);
static_assert(!std::is_trivially_destructible_v<NonTrivialConstexprDtor>);

static_assert(SupportsFieldNames<NonTrivialConstexprDtor>);
static_assert(TestGetFieldNames<NonTrivialConstexprDtor, "field"_ts>);

struct NonTrivialDtor {  // NOLINT(*-special-member-functions)

  ~NonTrivialDtor() { ++field; }

  int field{0};
};

static_assert(std::is_destructible_v<const NonTrivialDtor>);
static_assert(std::is_aggregate_v<NonTrivialDtor>);
static_assert(!std::is_trivially_destructible_v<NonTrivialDtor>);

static_assert(SupportsFieldNames<NonTrivialDtor>);
# if defined(__clang__)
static_assert(!SupportsFieldNamesConstexpr<NonTrivialDtor>);

TEST_F(StructNamesTest, StructWithNonTrivialDestructor) {
  EXPECT_THAT(GetFieldNames<NonTrivialDtor>(), ElementsAre("field"));
}
# else
static_assert(SupportsFieldNamesConstexpr<NonTrivialDtor>);
static_assert(TestGetFieldNames<NonTrivialDtor, "field"_ts>);
# endif  // defined(__clang__)

union DirectUnion {
  int integer;
  float floating;
};

static_assert(!SupportsFieldNames<DirectUnion>);
static_assert(GetFieldNames<DirectUnion>().empty());

# if defined(__GNUC__) && !defined(__clang__)

struct WithUnionMember {
  DirectUnion value;
  int tag;
};

static_assert(SupportsFieldNames<WithUnionMember>);
static_assert(TestGetFieldNames<WithUnionMember, "value"_ts, "tag"_ts>);

struct WithBitField {
  int value;
  unsigned bits : 3;
};

static_assert(!SupportsFieldNames<WithBitField>);

# endif  // defined(__GNUC__) && !defined(__clang__)

TEST_F(StructNamesTest, LocalType) {
  struct Local {
    int alpha;
    std::int64_t beta;
  };

  static_assert(SupportsFieldNames<Local>);
  static_assert(TestGetFieldNames<Local, "alpha"_ts, "beta"_ts>);
}

#else

static_assert(!kStructNameSupport);

#endif

}  // namespace
}  // namespace mbo::types::types_internal
