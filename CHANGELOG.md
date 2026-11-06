# Changelog

All important changes to Scrydantic are in this file. The format follows
[Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/). The version
numbers follow [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).
Before 1.0.0, a minor release can change the public API
([docs/architecture.md](docs/architecture.md#api-stability)).

The entries use ASD-STE100 Simplified Technical English.

## [Unreleased]

### Added

- **Structured decode errors** (`scrydantic/error.hpp`). `Error::issues`
  lists each issue that a decode found. Each `Issue` has an `IssueKind`, a
  path from the decoded value as `PathSegment`s, and a reason, for example
  `must be a number`. `json_path()` gives the text of a path. `max_issues`
  (64) is the maximum number of issues that one decode reports. Other
  failures, and all encode failures, keep `issues` empty.
- **Recursive models.** An aggregate can contain itself through
  `std::vector<T>` or `std::indirect<T>`, directly or through other
  aggregates and variant alternatives. The check accepts a cycle only if a
  value can end it. A cycle with no `std::optional`, `std::vector`, or
  variant alternative after the first fails to compile, and the message
  gives the path of the cycle. An encode stops at an array or object that is
  more than 256 levels deep, and the failure gives its path, so a deep value
  cannot overflow the stack.
- **`std::indirect<T>`.** It encodes, decodes, and has the schema of the `T`
  that it owns. `std::optional<std::indirect<T>>` is its nullable form, so
  `skip_null` and `emit_null` apply to it. `std::indirect<std::optional<T>>`
  fails to compile. An encode accepts any allocator, and a decode needs an
  allocator that can be default-constructed. An encode fails at the path of a
  `std::indirect` that a move made empty.
- **`$defs` and `$ref` in schemas.** The schema writes each aggregate that
  contains itself one time, under `$defs` of the root, and refers to it with
  `$ref`. A recursive root refers to itself as `"#"`, so it keeps
  `"type":"object"`. All other schemas are written in place, as before. The
  name of a definition comes from the qualified name of the type, for
  example `recursive.Node-string` for `recursive::Node<std::string>`. A type
  is not a `SupportedValue` if its schema defines two types with one name.
  It is also not a `SupportedValue` if no definition name can spell one of
  its recursive types: a class that is local to a function, a name that is
  not ASCII, or a template argument such as a pointer. The message gives the
  cause. The parameters of a tool share one `$defs`, so `bind()` checks their
  names together.

### Changed

- **Decode failures.** A decode does not stop at the first failure. It
  continues to the other members and elements, and it reports each issue.
  With one issue, `message` and `model_message` have the same text as
  before. With more issues, they have one `<path> <reason>` line for each
  issue. After `max_issues` issues, the decode stops, and a last line says
  that it did not report the other issues. A bound tool gives the issues to
  the host and the lines to the model.
- **Paths.** In decode and encode failures, a key that is not an ASCII
  identifier is a JSON string in brackets, for example `$["a.b"]`. Before,
  the key followed a `.`, so a key such as `a.b` looked like two steps.
- **Requirements.** GCC 16.1 or newer. CMake, `find_package(scrydantic)`, and
  the scripts reject GCC 16.0.x, which are development snapshots. CI now
  builds and tests with the released GCC 16.2.0.

## [0.1.0] - 2026-11-04

This is the first release.

### Added

- **Requirements.** GCC 16 or newer with `-freflection` (C++26 static
  reflection, P2996). CMake 3.30 or newer. The library uses only the C++
  standard library.
- **CMake targets.**
  - `scrydantic::json` is a static library in C++23. It does not use
    reflection.
  - `scrydantic::scrydantic` is a header-only C++26 library. It links
    `scrydantic::json`, and it sets `cxx_std_26` and `-freflection` for its
    consumers.
- **JSON core** (`scrydantic/json.hpp`).
  - A strict RFC 8259 parser. It rejects nesting deeper than 256 levels,
    invalid UTF-8, unpaired surrogate escapes, and numbers that overflow or
    underflow. It also rejects a byte order mark, NUL bytes, and data after
    the value. For a duplicate object key, the last value is used.
  - `is_valid_json()` is a validator that accepts the same input as the
    parser. The validator does not allocate memory.
  - A canonical writer. It writes no insignificant whitespace and sorts
    object keys by byte value. It writes only the string escapes that JSON
    requires, and the shortest number text that gives the same value.
  - `Json`, `JsonKind`, `JsonView`, `escape_json_string()`, and
    `format_json_number()`. Use `escape_json_string()` and
    `format_json_number()` to write strings and numbers as the canonical
    writer does.
- **Codec** (`scrydantic/codec.hpp`).
  - `encode()` writes a C++ value as canonical JSON. `decode<T>()` reads JSON
    into a C++ value. Each function returns `Result<T>`.
  - `encode_verbatim()` writes each `Json` payload with its own bytes.
    `encode()` writes each `Json` payload in canonical form.
  - `keys_v<T>` gives the JSON keys of an object type, in the order that
    `encode()` writes them. `tags_v<V>` gives the tags of a tagged variant, in
    alternative order.
  - Supported types: `bool`, integers of 64 bits or less, `float`, `double`,
    `std::string`, `std::string_view` (encode only), enumerations,
    `std::optional`, `std::vector`, `std::array`, tagged `std::variant`,
    aggregates, and `Json` for raw JSON payloads. `Json` has no schema.
  - A decode failure gives the JSON path and the cause, for example
    `$.shapes[0].radius must be a number`.
  - An unsupported type causes one `static_assert`. The message gives the
    member path and the cause.
- **JSON Schema** (`schema_v<T>` and `input_schema_v<T>`). The schema is a
  constant expression. It uses closed objects, `anyOf` for variants and
  optionals, and numeric bounds.
- **Annotations** (`scrydantic/annotations.hpp`): `description`,
  `description_of`, `name`, `tag`, `tool`, `skip_null`, `emit_null`, and
  `ignore_unknown`.
- **Tool binding** (`scrydantic/tools.hpp`).
  - `bind()` finds the functions with the `tool` annotation in a namespace or
    a `Toolbox` class. For each tool, it gives the name, the description, the
    input schema, and a type-erased handler.
  - `describe()` gives the same data at compile time, without handlers.
  - `bind_handler()` makes a handler from a callable object.
  - `tools_of()` and `toolbox_tools_of()` let a wrapper library forward the
    tool set that its caller sees.
  - A tool can receive a host call context as a first `const Context&`
    parameter.
  - A tool can refuse a call with `tool_error()`.
  - Each translation unit gets the tool set that it sees, because the tool
    name, the description, and the parameter names are template arguments.
- **Diagnostics** (`scrydantic/diagnostics.hpp`). Each concept has a function
  that gives the reason that a type fails the check, or an empty string when
  the type passes: `supported_value_diagnostic()`, `encodable_diagnostic()`,
  `decodable_diagnostic()`, `tool_arguments_diagnostic()`,
  `tool_handler_diagnostic()`, `toolbox_diagnostic()`, and
  `tools_diagnostic()`. A library that uses Scrydantic can give the same
  compile-time errors to its users.
- **Errors** (`scrydantic/error.hpp`): `Error`, `Result<T>`, `Status`, and
  `tool_error()`. `Error::model_message` contains only text that the model
  already has.
- **Version macros** (`scrydantic/version.hpp`): `SCRYDANTIC_VERSION_MAJOR`,
  `SCRYDANTIC_VERSION_MINOR`, `SCRYDANTIC_VERSION_PATCH`, `SCRYDANTIC_VERSION`,
  and `SCRYDANTIC_VERSION_STRING`. CMake reads the project version from this
  header.
- **Package.** `find_package(scrydantic 0.1 CONFIG)` finds the installed
  package. The package version file uses `SameMinorVersion`. You can also add
  the source tree with `FetchContent` or `add_subdirectory()`.

### Known limitations

- Only GCC 16 or newer can compile the reflection layer. Clang and MSVC do
  not have P2996 reflection.
- The codec does not encode or decode `std::int8_t` and `std::uint8_t`,
  because they are character types. Use `std::int16_t` or `std::uint16_t`.
- `Error` has no error category.
- Headers in `scrydantic/detail/` are not part of the public API. Any release
  can change them.

[Unreleased]: https://github.com/crybo-rybo/scrydantic/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/crybo-rybo/scrydantic/releases/tag/v0.1.0
