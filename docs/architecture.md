# Architecture

This document describes how Scrydantic is built and why. The usage guide is
the [README](../README.md), and the development workflow is in
[CONTRIBUTING.md](../CONTRIBUTING.md).

## Two layers

Scrydantic has two CMake targets. Its only dependency is the C++ standard
library.

| Target | What it is | Language |
|---|---|---|
| `scrydantic::json` | A static library: the strict JSON parser, the allocation-free validator, the canonical writer, and `JsonView`. The sources are in `src/json/`. | C++23 without reflection |
| `scrydantic::scrydantic` | A header-only INTERFACE library that links `scrydantic::json`. It contains the reflected type model, the family checks, schema generation, the codec, and tool binding. | C++26 with `-freflection` |

The JSON core parses untrusted bytes, so it stays plain C++23. The
consequences are as follows:

- clang-tidy can analyze the core (`scripts/lint.sh`).
- A consumer that does not enable reflection can link the core.
- The core's public headers (`error.hpp`, `json.hpp`, and
  `detail/json_text.hpp`) are compiled as C++23 by the header checks.
  Reflection syntax in these headers therefore fails the build.
- `src/json/core.hpp` makes the build fail if a core source is compiled with
  reflection.

The reflected layer sees parsed JSON only through `JsonView` and the text
operations in `detail/json_text.hpp`. The document type remains private in
`src/json/document.hpp`.

## Headers

| Header | Contents |
|---|---|
| `scrydantic.hpp` | An umbrella header that includes every other public header |
| `error.hpp` | `Error { message, model_message }`, `Result<T>`, `Status`, `tool_error()` |
| `json.hpp` | `Json`, `JsonKind`, `JsonView`, `escape_json_string()` |
| `annotations.hpp` | `description`, `description_of`, `name`, `tag`, `tool`, `skip_null`, `emit_null`, `ignore_unknown` (plain C++23 types) |
| `codec.hpp` | `encode()` and `decode()`. Through the detail headers, it also provides the concepts, `schema_v`, and `input_schema_v`. |
| `tools.hpp` | `bind()`, `bind_handler()`, `describe()`, `tools_of()`, `toolbox_tools_of()`, `Toolbox` |
| `detail/model.hpp` | The reflected type model: `kind_of`, annotations, field layouts, and static text |
| `detail/meta.hpp` | The value families, their diagnostics, and the handler rules |
| `detail/schema.hpp` | The consteval schema generator |
| `detail/decode.hpp`, `detail/encode.hpp`, `detail/failure.hpp` | The codec and its failure paths |
| `detail/tools.hpp`, `detail/binding.hpp` | Tool discovery and diagnostics, and handler generation |
| `detail/json_text.hpp`, `detail/json_string.hpp` | The core operations the codec calls (validate, canonicalize, spell a number) and the codec's string escaping |

## The codec

Every reflected operation asks the same type model, `detail/model.hpp`,
questions about a type: its shape (`kind_of`), each member's JSON key, whether
a member is required, and whether a member omits null. The schema, the encoder,
and the decoder therefore cannot disagree about a type.

**Family checks.** `value_checker` (in `detail/meta.hpp`) walks a type and
returns why the type is outside a family: `SupportedValue`, `Encodable`,
`Decodable`, or `ToolArguments`. An empty string means that the type is in the
family. Each entry point puts that text in a `static_assert`, and then uses
`if constexpr` to skip the body. As a result, a rejected type produces one
diagnostic that names the member path and the reason, not a cascade of errors.
The concepts are the SFINAE-friendly form of the same checks.

**Schemas** are consteval strings in `define_static_string` storage.
`schema_v<T>` and `input_schema_v<T>` are constant expressions.

**Decoding** reads a `JsonView`. A failure carries a path relative to the value
that failed (`codec_failure`). The path grows at the front as the failure
returns through each enclosing member or element. Thus, a successful decode
does not build any path text.

**Encoding** writes keys in canonical order and escapes strings in the
canonical way. Only two parts of the output have a choice of spelling: numbers
and spliced `Json` payloads. The `spelling` template argument makes this choice:

- `verbatim` writes the shortest text that `std::to_chars` produces for a
  number, and splices a payload byte for byte after the writer checks that the
  payload is JSON. Bound tool results use this spelling, so a host can bound
  a result's size before paying for a canonicalizing parse. It is the default
  of `detail::encode_value()`.
- `canonical` writes the text that canonicalizing the verbatim text would
  produce. `encode()` uses this spelling, followed by one validating pass that
  allocates nothing. Thus, `encode()` does not need to re-parse its own output.
  The differential tests in `tests/codec/codec_tests.cpp` check that the two
  spellings produce the same bytes. The tests use boundary doubles and floats,
  random bit patterns, and payloads.

The writer copies string bytes without checking them, and it checks each
spliced payload alone. The final validating pass, which every encoding does,
catches what is left: a string that is not UTF-8, and payloads that are each
valid but together nest deeper than the parser accepts. That pass reports
which of the two it found.

## Tools

`detail/tools.hpp` decides which functions are tools, how each tool is named,
and how a parameter list becomes one argument object. It also explains why a
declaration cannot be bound. `detail/binding.hpp` turns those answers into
`BoundTool`s.

**Call-site keying.** A namespace is open, and annotations accumulate across
redeclarations. Thus, two translation units can see different tools under one
name. Each fact that can differ between units is a template argument of the
`bound_tool` type: the tool name, the description, and the names of the
synthesized parameters. GCC mangles these arguments by their content, so
translation units that see different facts instantiate code with different
names. The `Tools` default of `bind()` and `describe()` records the view at the
call site. A library that wraps these functions computes the same default with
`tools_of()` or `toolbox_tools_of()`, and forwards the result.
`tests/multi_tu/` links one program in both orders to test this.

**Contexts.** A binding can give its tools a host-defined call context. A tool
receives the context as a leading `const Context&` parameter. The consteval
layer passes the context as a reflection, with `^^void` meaning "no context".

## Errors

`Error` carries `message` for the host and `model_message` for a model.
`model_message` is set only when every word of it comes from text that the
model already has. Strict decode failures use the words of the schema, so they
set both to the same `<path> <reason>` text, such as
`$.shapes[0].radius must be a number`. `tool_error()` sets `model_message` for
a tool's own refusals. Encode failures describe the program's own values, so
they carry only `message`. Scrydantic does not classify failures; the caller
knows where the input came from.

## Boundaries

`scripts/lint.sh` enforces these rules:

- A public header includes only standard headers and other Scrydantic headers.
- The JSON core includes only standard headers, its own headers, and the three
  C++23 public headers.
- An example includes only headers that a consumer of the installed package
  can include.
- A test can also include Catch2, local headers, and the core's private
  headers.

The CI `package` job builds a consumer that knows only
`find_package(scrydantic)`.

## API stability

The public API is what the headers outside `detail/` declare, together with
the concepts, `schema_v`, and `input_schema_v`, which `codec.hpp` makes
available. Everything in `scrydantic::detail` and under `detail/` is internal.
These headers are installed only because the public templates include them,
and any release may change them. A downstream project that reaches into
`detail` takes on that churn, and should pin an exact version.

Before 1.0, a minor release may break the public API too. The package's
version file accepts only the same minor series (`SameMinorVersion`).

## GCC 16 notes

Reflection in GCC 16 is new. The code avoids the following issues. Each
workaround is also commented where it is applied.

- **AddressSanitizer and consteval strings.** Under ASan, GCC cannot
  constant-fold the null checks that `std::string`'s pointer-taking constructor
  and `append` do on reflected static strings. For this reason, `owned_text()`
  copies such text one character at a time. The test helper `mentions()`
  avoids `string_view::find` for the same reason.
- **`define_static_array` of consteval-only class elements** rejects an array
  that is equal to an array it already made. `declared_members_v` is therefore
  an array of member reflections, not a second array of `field_layout`.
- **Generic lambdas** instantiate both branches of an `if constexpr` that
  depends on a template parameter of the enclosing function. Use a function
  template instead, as `call_tool_function` does.
- **Splices in mangled signatures** cannot be mangled. `invoke_tool_function`
  therefore deduces `Args` and does not spell it.
- **Uninstantiated specializations** answer class queries until something needs
  the specialization to be complete. `is_complete_class()` calls `size_of`
  first.
- **`annotations_of` throws** for templates and namespace aliases.
  `annotations_or_none()` catches the exception.
- **`return` and `break` work inside `template for`.** The codec uses them to
  stop at the first match or the first failure.
- **ADL.** In `scrydantic::detail`, an unqualified call whose name matches a
  public function that takes a Scrydantic type can also find the public
  overload. For example, `decode` finds `scrydantic::decode` through
  `JsonView`. Qualify such calls.
- **clang-format does not recognize `^^`.** Put a reflection in parentheses
  where it would otherwise merge with the next token: `(^^char)` and `(^^::)`.

## Performance

`tests/benchmarks/benchmarks.cpp` measures the core, the codec, and tool calls
on a document of about 25 KB (`cmake --workflow --preset release`). Its numbers
are the baseline for changes to the hot paths:

| Operation | Time per 25 KB document (one machine) |
|---|---|
| `validate_json` | about 20 µs |
| `JsonView::parse` | about 80 µs |
| `encode()` | about 75 µs (151 µs before the canonical writer) |
| `decode()` from a parsed view | about 70 µs |

A possible future optimization: each `JsonView::find()` and `JsonView::at()`
copies an aliasing `shared_ptr`. This copy costs two atomic operations for each
node that a decode visits. A borrowed traversal for the decoder would remove
this cost. However, the traversal would be a second way to read a document,
next to `JsonView`. Until a profile shows that the cost matters, `JsonView`
remains the only way to read a document.

## Open questions

- **`std::int8_t` and `std::uint8_t`.** These types are `signed char` and
  `unsigned char`, so `kind_of` rejects them as character types, and their
  diagnostic suggests `std::int16_t`. Treating the two types as integers
  would expand the supported set, but plain `char` would still be text.
- **`std::move_only_function`.** `ToolFunction` uses
  `std::move_only_function`, which libc++ on macOS does not provide. This does
  not matter while the reflection layer requires GCC and libstdc++.
- **Schema dialects.** The schemas are one portable subset of JSON Schema:
  closed objects, `anyOf` for variants and optionals, and numeric bounds. Some
  consumers need another shape; OpenAI's strict structured outputs, for
  example, require every property to be in `required`. A
  `schema_v<T, Dialect>` parameter is the natural extension point.
- **Error categories.** `Error` has no kind. If users need to distinguish
  failures, a small `enum class` is the obvious addition.
- **Package name.** Check `scrydantic` for name collisions before publishing.
