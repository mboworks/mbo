# Experimental string interning

This package owns strings, assigns dense identifiers, and supports parent/child snapshots.
All its types are in `mbo::strings::experimental`; its headers and Bazel targets are under
`mbo/strings/experimental`. APIs may change between releases without compatibility aliases or
deprecation. The independent generic-interner workstream is separate from this implementation.

| Header                     | Main types             | Purpose                                         |
| -------------------------- | ---------------------- | ----------------------------------------------- |
| `string_id.h`              | `StringId`             | Strong 8-, 16-, 32-, or 64-bit identifiers      |
| `arena_string_storage.h`   | `ArenaStringStorage`   | Stable character ownership and rollback         |
| `container_string_index.h` | `ContainerStringIndex` | Standard/Abseil map adapter for string lookup   |
| `string_interner.h`        | `StringInterner`       | Dense IDs, stable views, and captured ancestors |
| `string_interner_map.h`    | `StringInternerMap`    | Stable mapped objects alongside interned keys   |

Use `//mbo/strings/experimental:<header_basename>_cc` as the corresponding Bazel dependency.
HAMT-specific adapters live in the [HAMT experimental package](../../container/experimental/hamt/README.md),
under `mbo::container::experimental::hamt`.

```cpp
#include "mbo/strings/experimental/string_interner.h"

mbo::strings::experimental::StringInterner<> root;
auto root_id = root.try_intern_id("root");
mbo::strings::experimental::StringInterner<> child(&root);
auto child_id = child.try_intern_id("child");
auto inherited = child.find("root");  // Same ID as root_id.
```

`StringInterner` composes character storage, a dense descriptor table, and a replaceable index.
Defaults are `ArenaStringStorage`, the released `mbo::container::SegmentedVector<string_view>`,
and the flat HAMT string index. `StringInternerMap<Mapped>` adds a separate
`SegmentedVector<Mapped>`. These tables need append, indexed lookup, and back removal for rollback;
front operations would not help dense ID lookup. `SegmentedDeque` is therefore unnecessary.

The [API and lifetime contract](STRING_INTERNING.md) explains failure results, captured ancestor
visibility, backend requirements, and diagnostics. The [benchmark guide](STRING_INTERNER_BENCHMARKS.md)
describes index comparisons, storage profiles, workload coverage, and the review needed before
publishing new performance results. Previous charts and measurements were removed because they
predate this integrated implementation; their commits remain available in Git history.

For bounded storage, configure each allocation domain: characters, descriptors, index nodes and
control, and mapped values. `LimitedVector` can keep descriptors and mapped values inline.
`SegmentedVector` has a separate allocator-backed pointer directory: supplying a bounded element
block source alone does not eliminate heap allocation. Reserve/configure its directory as needed
or use `LimitedVector` when a fixed inline bound is required. Successful rollback can retain
allocated capacity even though logical contents and published addresses remain unchanged.
