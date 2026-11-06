# Architecture

This document describes how Scrydantic is built and why. The usage guide is
the [README](../README.md), and the development workflow is in
[CONTRIBUTING.md](../CONTRIBUTING.md).

## Two layers

Scrydantic has two CMake targets. Its only dependency is the C++ standard
library.

| Target | What it is | Language |
|---|---|---|
| `scrydantic::json` | A static library: the strict JSON parser, the allocation-free validator, the canonical writer, `JsonView`, and `json_path()`. The sources are in `src/json/`. | C++23 without reflection |
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
| `error.hpp` | `Error { message, model_message, issues }`, `Issue`, `IssueKind`, `PathSegment`, `json_path()`, `max_issues`, `Result<T>`, `Status`, `tool_error()` |
| `json.hpp` | `Json`, `JsonKind`, `JsonView`, `escape_json_string()` |
| `annotations.hpp` | `description`, `description_of`, `name`, `tag`, `tool`, `skip_null`, `emit_null`, `ignore_unknown`, the constraints `range`, `multiple_of`, `length`, `items`, and `pattern`, and `check` (plain C++23 types) |
| `codec.hpp` | `encode()` and `decode()`. Through the detail headers, it also provides the concepts, `schema_v`, and `input_schema_v`. |
| `tools.hpp` | `bind()`, `bind_handler()`, `describe()`, `tools_of()`, `toolbox_tools_of()`, `Toolbox` |
| `detail/model.hpp` | The reflected type model: `kind_of`, annotations, field layouts, static text, and which aggregates a schema defines once because they contain themselves |
| `detail/meta.hpp` | The value families, their diagnostics, and the handler rules |
| `detail/constraints.hpp` | Constraints and validators, resolved against their member's type: their diagnostics, schema keywords, and checks |
| `detail/schema.hpp` | The consteval schema generator |
| `detail/decode.hpp`, `detail/encode.hpp`, `detail/failure.hpp` | The codec and its failure paths |
| `detail/tools.hpp`, `detail/binding.hpp` | Tool discovery and diagnostics, and handler generation |
| `detail/json_text.hpp`, `detail/json_string.hpp`, `detail/json_number.hpp` | The core operations the codec calls (validate, canonicalize, spell a number), the codec's string escaping, and the canonical spelling of a number at compile time |

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

The checker visits each aggregate once, at the path where it first reaches the
aggregate. Thus, an aggregate that contains itself ends its own walk, and an
aggregate that many members share is not walked again.

**Recursive models.** An aggregate can contain itself through `std::vector<T>`,
which allows an incomplete element type, or through `std::indirect<T>`. An
indirect is the value it owns: it encodes, decodes, and has the schema of that
value. `std::optional<std::indirect<T>>` is its nullable form, so `skip_null`
and `emit_null` apply to it, and `std::indirect<std::optional<T>>` is rejected.
Its allocator is treated as `std::vector`'s is: any allocator encodes, and
decoding needs only the default construction that `is_storable` checks.

A cycle is admitted when it can end. After the shape check passes, a second walk
follows only the links that default construction fills: every member, the value
of a `std::indirect`, every element of a non-empty `std::array`, and the first
alternative of a variant. It leaves out `std::optional`, `std::vector`, an empty
`std::array`, and later alternatives. Decoding starts from a default-constructed
value, so a cycle of filled links alone never ends, and the checker rejects it
with the path of the cycle. A value to encode can hold any alternative of a
variant, so for `Encodable` the walk does not enter variants at all. A cycle
without a variant then means that every value is infinite or, through a
reference member, cyclic. Reflection cannot see what a default member
initializer holds, so the walk assumes that each member starts as its type's
default. A non-empty `std::array<T, N>` cannot hold its own aggregate `T`, which
is incomplete inside its own definition, but `std::array<std::indirect<T>, N>`
and `std::array<std::vector<T>, N>` can form a cycle. The walk visits each
aggregate once, as the shape check does.

**Schemas** are consteval strings in `define_static_string` storage.
`schema_v<T>` and `input_schema_v<T>` are constant expressions.

A schema is one tree, written in place, except where a type contains itself.
Many model APIs handle `$ref` poorly, so the schema of a type without recursion
has no `$ref` and no `$defs`. The unit of recursion is an aggregate in one of
its two forms: a plain value, or a variant alternative, whose object also
carries the tag. Each form has its own schema. Every reachable form that can
contain itself is written once, under the root's `$defs`, and every other use
of it is `{"$ref":"#/$defs/<name>"}`. The root is never moved into `$defs`. A
recursive root refers to itself as `{"$ref":"#"}`, so it keeps its own keywords,
such as the `"type":"object"` that tool APIs require of an input schema, and a
type whose only recursion is the root needs no `$defs` at all. `$defs` sorts
before every other key, so it opens the root object.

A definition's name is spelled from reflection, not from a display string, so
it does not change when the compiler prints a type differently, and it does not
show a model the standard library's internal names. `detail/model.hpp` states
the grammar where it defines `definition_name()`:

```text
definition = class [ "-untagged" ]
class      = { scope "." } identifier { "-" argument }
scope      = namespace-identifier | "anonymous" | class
argument   = type | constant
type       = "bool" | "char" | "wchar_t" | "char8_t" | "char16_t" | "char32_t"
           | ( "int" | "uint" ) width | "float" | "double" | "string"
           | ( "optional" | "vector" | "indirect" ) "-" type
           | "array" "-" type "-" constant | "variant" "-" type { "-" type }
           | class
constant   = [ "minus" ] decimal | "true" | "false" | enumerator-identifier
```

A class argument is an aggregate or an enum. An integer type is named by its
width in bits, so a name is the same on every platform, and a vector or an
indirect must use `std::allocator`. For example:

| Type | Definition name |
|---|---|
| `recursive::Node<std::string>` | `recursive.Node-string` |
| `recursive::Node<int>` | `recursive.Node-int32` |
| `Outline` in an anonymous namespace | `anonymous.Outline` |
| `recursive::Spin<-1>` | `recursive.Spin-minus1` |
| `recursive::Outer<float>::Inner` | `recursive.Outer-float.Inner` |
| the plain form of a tagged `recursive::Folder` | `recursive.Folder-untagged` |

Every character of a name is in `[A-Za-z0-9_.-]`. These characters are
unreserved in a URI fragment, have no meaning in a JSON Pointer (RFC 3986, RFC
6901), and are the characters that OpenAPI accepts in a component name. A
reference therefore never needs escaping. An identifier with any other
character, which GCC accepts in UTF-8, is rejected rather than percent-encoded,
because the `$defs` key itself would then leave that set. A class local to a
function is rejected too, because its only stable identity is the signature of
its function, and a lambda has none. So is a template argument outside the
grammar, such as a pointer or a floating-point constant. These rejections apply
only to recursive types, since every other type is written in place and needs
no name.

Collisions remain possible. A name does not mark where an argument list ends,
`long` and `long long` have one width, a namespace can be named `anonymous`,
and a class at global scope can be named `string`. `SupportedValue` therefore
rejects a type whose schema would define two types under one name, and names
both. A tool's synthesized argument object holds every parameter's definitions
in one `$defs`, so binding checks the parameters' names together and names the
function.

A described member whose schema is a reference is
`{"$ref":"...","description":"..."}`. JSON Schema 2019-09 and 2020-12 apply a
keyword next to `$ref`. Draft-07 ignores it, which loses only the annotation,
since the reference has no other keyword next to it. Draft-07 does not know
`$defs` either, but it still resolves a JSON Pointer into it, so draft-07 and
2020-12 validators accept the same documents. Wrapping a described reference in
`allOf` would keep the description under draft-07, at the cost of a keyword
that model APIs support less widely than `$ref`.

**Decoding** reads a `JsonView`, and a failure does not stop it. Each read
function takes a `decode_cursor`, which holds the path to its value as a chain
of cursors: one in each call that descends into a member or element, holding a
key borrowed from `fields_v` or an index. A failure reports an `Issue` to the
decode's `decode_report`, which copies the chain into an owned path, and the
decoder goes on to the siblings of the value that failed. Thus, a successful
decode builds no path and allocates nothing for one. The report keeps
`max_issues` issues, and the first issue past them stops the decode, so the work
that hostile input causes ends at its 65th issue.

An aggregate is read in place. Its reader fills a value that `read_in_place()`
value-initializes (`Type()`), as `is_storable` checks, and does not
list-initialize. `Type{}` would copy-initialize a member that has no
initializer from `{}`, which the explicit default constructor of `std::indirect`
does not allow. `read_in_place()` is the one place that builds an aggregate's
result; the GCC 16 notes say why that matters. A variant is built from the
alternative that its tag selects, which is read in place, and is not itself read
in place: that would default-construct its first alternative only to replace
it, which cost 1.5% of a decode. The parser accepts at most
256 levels of nesting, and every cycle passes through an aggregate, which reads
one JSON object. Thus, the decoder's recursion is bounded by the input's
nesting times a constant of the type.

**Constraints** (`range`, `multiple_of`, `length`, `items`, `pattern`) are
resolved once against the type of the member that carries them, or the value
type of an optional member, each seen through any `std::indirect` as the codec
sees it, in `detail/constraints.hpp`, and that one resolution feeds three
readers. The
family checks reject a constraint that cannot hold: one on a type it does not
apply to, a second of one kind, a range that is empty or crossed, and a bound
that is not exactly a value of the member's type. The schema generator writes
each as its JSON Schema keyword, in sorted position, inside an optional's
`anyOf`, and in place of an integer type's own bound, so an integer has one
lower and one upper bound. An item bound sits beside `"items"`, which for a
recursive element is a `$ref`, and a definition under `$defs` carries its
members' keywords as a root does. The decoder checks each one but `pattern` on a
member's decoded value, and reports a miss as `violated_constraint` in the
keyword's words. The schema and the check therefore bound the same values.

That needs a bound to be one number on both sides. A `detail::bound` keeps an
integer as a sign and a magnitude and a floating-point value as a double, so it
holds any literal exactly, and the checks require it to be exactly a value of
the member's type, so the schema spells the same value that the decoder
compares with, a float's as the double it is. A floating-point `multiple_of`
must also be a binary fraction whose decimal spelling is exact, since JSON Schema
tests decimal multiples and `fmod` binary ones.

An integer member is compared in its own type, which holds the parsed integer
exactly. A floating-point member is compared twice. First the number as the
parser read it, an integer exactly and anything else as its double, against the
bound, a comparison that is exact because the bound is a double too. So
decoding rejects whatever the schema rejects, where comparing the converted
value would let `2.50000001` round to `2.5f` and meet `"maximum":2.5`. Then the
converted value, which the host gets: conversion rounds monotonically to values
of the member's type, of which the bound is one, so it can carry a number onto
an exclusive bound or off a multiple, never past an inclusive bound. That
second miss has its own reason,
`must be less than 2.5 even once rounded to single precision`, because the
bound's reason alone would contradict the model's own number, which does meet
it, and invite the same number again. JSON text more precise than a double is
rounded by the parser before either check, as it is for any decoder that reads
numbers as doubles.

The schema spells a double as the canonical writer does, which reads its digits
from `std::to_chars`. That is not constexpr for floating-point types, so
`detail/json_number.hpp` finds the same shortest round-trip digits by exact
decimal arithmetic, and a differential test holds the two spellings to the same
bytes.

`pattern` is written to the schema and never checked: JSON Schema's patterns are
ECMA-262 regular expressions, which `std::regex` neither matches exactly nor
runs at compile time. It is a contract for the producer, and a host that relies
on it checks it in a validator.

**Validators** are functions that `check<&fn>` names, on a member or a class.
They state nothing in the schema. A member's validators run once its value has
decoded and met its constraints, a class's once every member of an object is
valid, each only if the ones before it accepted the value. A class's run last
in `read_object()`, which returns whether the object passed, so no aggregate's
failure is converted from a `std::unexpected` (see the GCC 16 notes), and in a
recursive model each level's run once the levels inside it have passed theirs.
A rejection is a `failed_check` issue. Since a validator runs only where nothing has been
reported for its value, it runs only while the decode goes on, as the stop rule
requires, and the decode after the limit calls none: 100 failing elements cost
65 validator calls. A validator returns its rejection; an exception from one is
an allocation failure and leaves the decode. Contracts (`pre` and `post`) would
be the wrong tool: they are for the program's own errors, not untrusted input,
and GCC needs `-fcontracts` for them, which every consumer would inherit.

A member that keeps its default is not checked, and neither is any value being
encoded: both are the program's own, and an encoder that rejected them would be
a different feature.

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

The writer counts the arrays and objects that are open around each value. A
recursive value can nest without bound, so the writer refuses, at its path, the
array or object that the parser would refuse. It stops before its own recursion
can exhaust the stack. A `std::indirect` that a move has left without a value
fails at its path, as a variant that an exception has left valueless does.

The writer copies string bytes without checking them, and it checks each
spliced payload alone. The final validating pass, which every encoding does,
catches what is left: a string that is not UTF-8, and a payload that is valid
but nests deeper than the parser accepts inside what encloses it. That pass
reports which of the two it found.

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

**Parameter annotations.** A synthesized argument object is built from the
parameters' types and names alone, so a binding reads no annotation on a
parameter. A tool whose model-supplied parameter carries a Scrydantic
annotation fails to bind, rather than silently drop a constraint or a check; a
constrained argument is a member of an argument aggregate. Reading them would
make parameter annotations, which accumulate across redeclarations, one more
fact that `bound_tool` keys.

## Errors

`Error` carries `message` for the host, `model_message` for a model, and
`issues` for a program. `model_message` is set only when every word of it comes
from text that the model already has. `tool_error()` sets it for a tool's own
refusals.

**Decode failures** list every issue that strict decoding found, in the order
that it visits them: an object's unknown members in key order, then its
declared members in declaration order, and an array's elements in index order.
Each `Issue` has an `IssueKind`, a path from the decoded root as
`PathSegment`s, and a reason in the words of the schema, such as
`must be a number` or `must be at most 150`. Both messages are one
`<path> <reason>` line for each issue, such as
`$.shapes[0].radius must be a number`, joined by newlines.

A validator's rejection is the one place where the two messages differ, because
its `message` is the host's own text, which the model must not see. The issue's
reason is what the model may see: the rejection's `model_message`, or, when that
is empty, `is not valid`, which tells the model where its call went wrong
without a word of why. Leaving the line out instead would leave the model with
a failure it cannot place, or with no text at all when that was the only issue.
The report keeps the host's text beside the issue, and `message` gives it in
that line's place. Every `Issue` reason is therefore text a model may see, so a
host can build its own model feedback from `issues` and leak nothing.

A failure that leaves nothing to read inside a value is that value's only
issue: a value of the wrong JSON type, a fixed-size array of the wrong length,
and a tagged variant whose tag is missing or unknown, which chooses no
alternative to check the object against. Text that is not JSON is one
`invalid_json` issue at `$`, with messages that name the input:
`JSON text is not valid JSON` from `decode()` and
`tool arguments are not valid JSON` from a bound tool.

A decode keeps at most `max_issues` (64) issues: more than a model or a person
acts on at once, and few enough that hostile input cannot make a decode do much
work or write much text. The first issue past them stops the decode, and both
messages end with the line
`further issues past the first 64 were not reported`. That line is text only,
not an issue, so every `Issue` is a failure of the input.

`json_path()` spells a path as JSONPath: `.key` for a key that is an ASCII
identifier, `["key"]` with any other key as a JSON string literal, so that a
key such as `a.b` cannot be read as two steps, and `[index]` for an element.

**Encode failures** describe the program's own values, so they carry only
`message`, and `issues` stays empty. Encoding stops at the first one: there is
no input to correct, only a value to fix. Its path is collected as the failure
returns through each enclosing member or element, and spelled by
`json_path()`.

`IssueKind` says what kind of failure each issue is. Whether the input came
from a user, a model, or a peer is left to the caller, which knows.

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
- **Odr-used inline variables of consteval-only type.** GCC 16.1 rejects one as
  "not defined", although it is (PR c++/124770, fixed in 16.2). A table of
  reflections that consteval functions iterate, such as
  `constraint_annotations()`, is therefore a consteval function, not an
  `inline constexpr` variable. Without `inline`, the variable would have
  internal linkage, and the inline functions that use it would break the ODR.
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
- **`std::expected` asks too much of a recursive aggregate.** The converting
  constructor of libstdc++'s `std::expected` asks whether `T` is constructible
  from its argument before it rules out a `std::unexpected`. For an aggregate
  whose first member is `std::optional<std::indirect<Self>>`, that question asks
  itself through the constraints of the forwarding constructors of
  `std::optional` and `std::indirect`, which is a hard error. A variant with
  such an alternative asks it too. Aggregates are therefore read in place, and
  `read_in_place()` builds their failed result with `std::unexpect`, through
  `decode_failed()`, as the variant reader builds its own. The decode entry
  points compose `transform_error()` and `and_then()`, which build failures the
  same way.
- **`return` and `break` work inside `template for`.** The codec uses them to
  stop at the first match, at an encode failure, or when a decode stops.
- **`-Wshadow` and `template for`.** In a function that is not a template, GCC
  warns that each expansion of a `template for` shadows the one before it. The
  test helper `enumerator_name()` is a function template for this reason.
- **Empty error types in `std::expected`.** At `-O2`, GCC warns
  (`-Wmaybe-uninitialized`) that `std::expected` copies the byte of an empty
  error type, which nothing writes. `decode_failure` is therefore an
  enumeration, not an empty struct.
- **Inlining and `std::variant`.** GCC does not inline a small function that
  constructs a `std::variant` into a large caller, such as the unrolled
  `read_object` of a class with many members. `decode_cursor` therefore keeps
  its step as a key and an index, not as a variant; the calls cost 4% of a
  decode.
- **ADL.** In `scrydantic::detail`, an unqualified call whose name matches a
  public function that takes a Scrydantic type can also find the public
  overload. For example, `decode` finds `scrydantic::decode` through
  `JsonView`. Qualify such calls.
- **Null function pointers under UBSan.** With `-fsanitize=null`, which
  `-fsanitize=undefined` enables, GCC does not constant-fold the comparison of
  a function's address with `nullptr`. `is_null_pointer()` therefore compares
  the validator's reflection with that of a null pointer of its type.
- **`-Wfree-nonheap-object` and `std::expected`.** At `-O2`, GCC warns that
  `operator delete` is called on an unallocated object when a `std::expected`
  whose value owns memory has its error moved out after a check that GCC cannot
  see ends the function, such as a test framework's assertion: on the path where
  the expected holds a value, the moved error's short strings point into the
  storage that the value's destructor then frees. The warning comes from
  `std::expected<std::vector<int>, std::string>` alone, so it is no Scrydantic
  defect, and a caller of `decode()` meets it only by calling `error()` on a
  result it has not branched on. Branch on the result, or copy the error, as
  the test helper `rejection()` does.
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
  closed objects, `anyOf` for variants and optionals, numeric bounds, the
  constraint keywords, and `$defs` and `$ref` for recursive types only. Some
  consumers need another shape; OpenAI's strict structured outputs, for example,
  require every property to be in `required`. A `schema_v<T, Dialect>`
  parameter is the natural extension point.
- **Constraints on elements and parameters.** A constraint bounds a member's
  own value, or an optional member's engaged value, seen through any
  `std::indirect`. Bounding each element of a
  vector needs an aggregate element type today, and a tool parameter cannot
  carry one at all (see [Parameter annotations](#tools)). Either would need a
  place to write the annotation that the type model can key.
- **Package name.** Check `scrydantic` for name collisions before publishing.
