# `mbo/log`

- Namespace: `mbo::log`.
- Scope: type-name demangling, scoped output, elapsed-time logging.
- Build definitions: [BUILD.bazel](BUILD.bazel).

## Headers and targets

| Header                                   | Bazel target                    | API                                             | Visibility      |
| ---------------------------------------- | ------------------------------- | ----------------------------------------------- | --------------- |
| [demangle.h](demangle.h)                 | `//mbo/log:demangle_cc`         | `Demangle`, `DemangleV`                         | Public          |
| [log_timing.h](log_timing.h)             | `//mbo/log:log_timing_cc`       | `LogTiming`                                     | Public          |
| [scoped_stream.h](scoped_stream.h)       | `//mbo/log:scoped_stream_cc`    | Scoped streams and output suppression           | Public          |
| [scoped_log_check.h](scoped_log_check.h) | `//mbo/log:scoped_log_check_cc` | `MBO_LOG_CHECK`, demonstration `ScopedLogCheck` | Package-private |

## Demangling

| Function           | Input                            | Result                                                                     |
| ------------------ | -------------------------------- | -------------------------------------------------------------------------- |
| `Demangle(name)`   | NUL-terminated mangled type name | Demangled `std::string`; original name on failure or unsupported platforms |
| `DemangleV(value)` | Object expression                | `Demangle(typeid(value).name())`                                           |

- Demangling backend: `abi::__cxa_demangle` on supported builds.
- Type-name spelling: compiler and ABI dependent.

## Timing

- `LogTiming(...)`: returns an RAII object; destruction logs elapsed time through Abseil.
- Named local object: measures until its scope ends.
- `<<`: appends message parts using `absl::StrAppend`.
- Move construction: transfers the pending log; copying and assignment are disabled.
- Argument form: designated aggregate initialization; the argument type is internal.

| Argument field | Default                           | Meaning                                             |
| -------------- | --------------------------------- | --------------------------------------------------- |
| `severity`     | `absl::LogSeverity::kInfo`        | Log severity                                        |
| `min_duration` | `std::nullopt`                    | Per-call duration threshold; otherwise use the flag |
| `start_time`   | `absl::Now()`                     | Start of the measured interval                      |
| `src`          | `std::source_location::current()` | Reported caller location                            |

| Abseil flag                            | Default | Meaning                                              |
| -------------------------------------- | ------- | ---------------------------------------------------- |
| `--mbo_log_timing_min_duration`        | `2s`    | Suppress shorter durations                           |
| `--mbo_log_timing_min_severity_always` | `ERROR` | Log at or above this severity regardless of duration |

```cpp
#include "absl/time/time.h"
#include "mbo/log/log_timing.h"

void Process();

void TimedProcess() {
  auto timing = mbo::log::LogTiming({.min_duration = absl::Milliseconds(50)}) << "Process";
  Process();
}
```

## Scoped output

| API                                                     | Destination or behavior                                      |
| ------------------------------------------------------- | ------------------------------------------------------------ |
| `ScopedStreamOut()`                                     | `std::cout`                                                  |
| `ScopedStreamErr()`                                     | `std::cerr`                                                  |
| `ScopedStreamVoid()`                                    | Discard output                                               |
| `ScopedStream<Mode, StringStreamT, OStreamT, MessageT>` | Configurable message collection and destination              |
| `VoidStream`                                            | Discard `<<` values; empty `str()`; false boolean conversion |
| `Voidifier`                                             | Empty placeholder for omitted stream/message state           |

- Output time: destruction of the scoped stream.
- Prefix: `[file:line] @function`; optional message and collected text separated by a colon with one
  space on each side.
- Terminator: newline.
- Location argument: `std::source_location`, captured at the factory call by default.
- Copying and moving: disabled for `ScopedStream`.
- Custom destination stream and `std::string_view` message: must outlive the scoped stream.
- Suppression discards output; ordinary C++ argument expressions still evaluate.

| `ScopedStreamMode` | Destruction behavior                                        |
| ------------------ | ----------------------------------------------------------- |
| `kContinue`        | Emit configured output and continue                         |
| `kQuickExit`       | Emit and flush configured output, then `std::quick_exit(1)` |

```cpp
#include "mbo/log/scoped_stream.h"

void ReportCount(int count) {
  mbo::log::ScopedStreamErr() << "Processed " << count << " items";
}
```

## Package-private checks

- `MBO_LOG_CHECK(condition) << message`: evaluate the condition; create the fatal stream only on failure.
- Failure behavior: write to `std::cerr`, flush, and call `std::quick_exit(1)`.
- `ScopedLogCheck`: explicitly marked as demonstration-only in its header.

## Tests

```sh
bazel test --config=clang //mbo/log:all
```

- [demangle_test.cc](demangle_test.cc), [log_timing_test.cc](log_timing_test.cc).
- [scoped_stream_test.cc](scoped_stream_test.cc), [scoped_log_check_test.cc](scoped_log_check_test.cc).
