# Scrydantic guide

Everything is in one header, `<scrydantic/scrydantic.hpp>`, and in the
`scrydantic` namespace. The examples below leave out the `scrydantic::` prefix
in prose, but not in code.

- [Models](#models)
- [Annotations](#annotations)
- [Schemas](#schemas)
- [Encoding](#encoding)
- [Decoding and errors](#decoding-and-errors)
- [Constraints and validators](#constraints-and-validators)
- [Tools](#tools)
- [Working with raw JSON](#working-with-raw-json)
- [Compile-time errors](#compile-time-errors)

## Models

A model is a plain aggregate: a struct with public, named members and no
constructors of its own. Its members can be any of these types:

| C++ type | JSON |
|---|---|
| `bool` | `true` or `false` |
| integers, such as `int` or `std::uint64_t` | an integer within the type's range |
| `float`, `double` | a finite number |
| `std::string` | a string |
| a scoped `enum class` | the enumerator's name, as a string |
| `std::optional<T>` | `T` or `null` |
| `std::vector<T>` (not `vector<bool>`) | an array |
| `std::array<T, N>` | an array of exactly `N` elements |
| `std::indirect<T>` | whatever `T` is |
| another aggregate | an object |
| `std::variant<A, B, ...>` of tagged aggregates | an object with a `"type"` member |
| `scrydantic::Json` | any JSON value, kept as text |

`std::int8_t` and `std::uint8_t` are character types in C++, so they are
rejected. Use `std::int16_t`. `char` and the other character types are rejected
too. Encoding also accepts `std::string_view` and reference members, which
decoding cannot fill.

**Which members are required.** A member without a default member initializer
must be present in the input. A member with one, even `{}`, is optional, and
keeps its default when it is absent:

```cpp
enum class Unit { celsius, fahrenheit };

struct Reading {
  double degrees;                   // required
  Unit unit{Unit::celsius};         // optional: absent means celsius
  std::optional<std::string> label; // required, but may be null
};
```

To make an optional member also optional on input, see `skip_null` below.

**Recursion.** A model can contain itself through `std::vector<T>` or
`std::optional<std::indirect<T>>`:

```cpp
struct Comment {
  std::string text;
  std::vector<Comment> replies;
};
```

A type whose default value would be infinite, such as one that holds a plain
`std::indirect` of itself, does not compile.

## Annotations

Annotations are C++26 attributes of the form `[[= value]]`. A member or a
function takes them before its declaration. A class takes them between `struct`
and its name: `struct [[= scrydantic::skip_null]] Profile { ... };`.

| Annotation | Goes on | What it does |
|---|---|---|
| `description{"..."}` | member | Sets the member's `description` in the schema. |
| `name{"..."}` | member, tool | Replaces the member's JSON key, or the tool's name. |
| `tag{"..."}` | class | The class's `"type"` value when it is a variant alternative. |
| `skip_null` | class, optional member | Leaves out an empty optional instead of writing `null`, and lets the input leave it out. On a class, it applies to every optional member. |
| `emit_null` | optional member | Writes `null` again for one member of a `skip_null` class. |
| `ignore_unknown` | class | Decoding ignores members the class does not declare, instead of rejecting them. |
| `tool{"..."}` | function | Makes the function a tool, with this description. |
| `range`, `multiple_of`, `length`, `items`, `pattern` | member | Constraints. See [Constraints and validators](#constraints-and-validators). |
| `check<&fn>` | member, class | Runs a validator after decoding. |

`description_of<text>()` builds a description from a namespace-scope
`constexpr std::string_view`, for text you keep in one place.

```cpp
struct [[= scrydantic::skip_null]] Profile {
  [[= scrydantic::name{"display_name"}]] std::string name;
  std::optional<std::string> email;                        // omitted when empty
  [[= scrydantic::emit_null]] std::optional<std::string> phone; // null when empty
};

scrydantic::encode(Profile{.name = "Ada"})->text;
// {"display_name":"Ada","phone":null}
```

**Variants.** Every alternative of a `std::variant` must be an aggregate with a
distinct `tag`. The encoded object carries the tag under the key `"type"`, so an
alternative cannot have a member whose key is `"type"`.

```cpp
struct [[= scrydantic::tag{"circle"}]] Circle { double radius; };
struct [[= scrydantic::tag{"square"}]] Square { double side; };
using Shape = std::variant<Circle, Square>;
// Shape{Circle{1.5}} encodes as {"radius":1.5,"type":"circle"}
```

## Schemas

`schema_v<T>` is the JSON Schema of `T`, as a `constexpr std::string_view`:

```cpp
scrydantic::schema_v<Reading>
// {"additionalProperties":false,
//  "properties":{"degrees":{"type":"number"},
//                "label":{"anyOf":[{"type":"string"},{"type":"null"}]},
//                "unit":{"enum":["celsius","fahrenheit"],"type":"string"}},
//  "required":["degrees","label"],"type":"object"}
```

(The real string has no whitespace.) The schemas use a portable subset of JSON
Schema that model APIs accept: objects are closed, optionals and variants use
`anyOf`, and integers carry their type's `minimum` and `maximum`.

A schema is written in place, with no `$ref`, unless the type is recursive.
Then every type that contains itself is defined once under `$defs`, and a
recursive root refers to itself as `{"$ref":"#"}`:

```cpp
scrydantic::schema_v<Comment>
// {"additionalProperties":false,
//  "properties":{"replies":{"items":{"$ref":"#"},"type":"array"},
//                "text":{"type":"string"}},
//  "required":["replies","text"],"type":"object"}
```

`input_schema_v<Args>` is the schema of a tool's argument object. It is the
same as `schema_v<Args>`, but `Args` must be an aggregate.

## Encoding

`encode(value)` returns a `Result<Json>`: the canonical JSON text, or an
`Error`. Keys are in sorted order, and numbers and strings have one spelling
each, so equal values always encode to equal bytes.

```cpp
auto json = scrydantic::encode(Reading{.degrees = 21.5});
if (json) {
  json->text; // {"degrees":21.5,"label":null,"unit":"celsius"}
}
```

Encoding fails only for values that have no JSON form: a NaN or an infinity, an
enum value that is not a declared enumerator, a string that is not UTF-8, an
invalid `Json` payload, or nesting deeper than 256 levels. It does not check
constraints or validators, because the value is the program's own.

`encode_verbatim(value)` is the same, except that a `Json` member is copied
byte for byte rather than re-spelled. Use it when a payload holds numbers that
a `double` cannot hold exactly. Tool results are encoded this way.

## Decoding and errors

`decode<T>(json)` parses JSON text and returns a `Result<T>`. It also accepts
a `JsonView` that you have already parsed.

```cpp
auto reading = scrydantic::decode<Reading>(scrydantic::Json{.text = R"({"degrees":3,"label":null})"});
if (reading) {
  reading->degrees; // 3
}
```

Decoding is strict. A value of the wrong type, a number out of range, a missing
required member, an unknown member, an unknown enumerator or tag, and an array
of the wrong length are all errors. Decoding does not stop at the first one.
It reports up to 64 (`max_issues`), and then stops.

A failure is an `Error` with three parts:

- `message`: one line for each issue, for your logs.
- `model_message`: the same lines, safe to send back to a model. Every word in
  it comes from the schema, the input, or a validator's model-facing text.
- `issues`: the same issues as data. Each `Issue` has a `kind` (an
  `IssueKind`), a `path`, and a `reason`. `json_path(issue.path)` spells the
  path, such as `$.shapes[0].radius`.

```cpp
auto drawing = scrydantic::decode<Drawing>(scrydantic::Json{
    .text = R"({"shapes":[{"type":"circle","radius":"big"},{"type":"hex"}],"extra":1})"});
drawing.error().message;
// $ contains unknown member "extra"
// $.shapes[0].radius must be a number
// $.shapes[1].type must be one of: circle, square
```

`Result<T>` is `std::expected<T, Error>`, and `Status` is `Result<void>`. Check
a result before you use its value or its error. At `-O2`, GCC can give a false
`-Wfree-nonheap-object` warning when you move the error out of a result that
you have not branched on. Copy it instead.

## Constraints and validators

Constraints bound a member in its schema and in decoding at the same time:

| Annotation | For | Schema keywords |
|---|---|---|
| `range{.min, .max, .exclusive_min, .exclusive_max}` | numbers | `minimum`, `maximum`, `exclusiveMinimum`, `exclusiveMaximum` |
| `multiple_of{n}` | numbers | `multipleOf` |
| `length{.min, .max}` | `std::string` | `minLength`, `maxLength` (counted in code points) |
| `items{.min, .max}` | `std::vector` | `minItems`, `maxItems` |
| `pattern{"..."}` | `std::string` | `pattern` |

On a `std::optional` member, a constraint applies to the value when there is
one. `pattern` is written to the schema only. Decoding does not check it,
because `std::regex` does not match JSON Schema's regular expressions exactly.
If you rely on a pattern, check it in a validator too.

A constraint that cannot hold does not compile: `range{.min = -1}` on an
unsigned member, `range{.max = 0.1}` on a `float` (write `0.1f`), or
`multiple_of{0.1}`, since no `double` is exactly one tenth.

A validator is a function that takes `const T&` and returns a `Status`.
`check<&fn>` runs it on a member after the member decodes and meets its
constraints. On a class, it runs on the whole object after every member has
decoded. Return `tool_error(model_text, host_text)` to reject a value. The model
sees only `model_text`, and your logs get `host_text`:

```cpp
inline scrydantic::Status known_city(const std::string& city) {
  if (city == "Oslo" || city == "Lima") {
    return {};
  }
  return std::unexpected(
      scrydantic::tool_error("must be a known city", "has no row in cities"));
}

struct Booking {
  [[= scrydantic::check<&known_city>]] std::string city;
  [[= scrydantic::range{.min = 1, .max = 8}]] std::int32_t guests;
  [[= scrydantic::length{.max = 200}]] std::string note{};
};
```

The schema of `guests` is `{"maximum":8,"minimum":1,"type":"integer"}`.
Decoding `{"city":"Rome","guests":9}` fails with:

```text
model_message                  message
$.city must be a known city    $.city has no row in cities
$.guests must be at most 8     $.guests must be at most 8
```

A validator reports a rejection by returning it. Do not throw from one.

## Tools

Annotate functions with `tool`, then bind them. `bind<^^ns>()` binds every tool
in a namespace, in declaration order, and `bind<^^fn>()` binds one function:

```cpp
namespace weather {
[[= scrydantic::tool{"Get the forecast for a city"}]]
Reading forecast(std::string city, std::int32_t days);
}

std::vector<scrydantic::BoundTool<>> tools = scrydantic::bind<^^weather>();
```

Each `BoundTool` has a `name`, a `description`, an `input_schema`, and an
`invoke` handler. The handler takes the arguments as `Json` and returns a
`Result<Json>`:

```cpp
tools[0].input_schema.text;
// {"additionalProperties":false,
//  "properties":{"city":{"type":"string"},
//                "days":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},
//  "required":["city","days"],"type":"object"}

auto result = tools[0].invoke(scrydantic::Json{.text = R"({"city":"Oslo"})"});
result.error().model_message; // $.days is a required member
```

**Parameters.** A tool can take no parameters, any list of named parameters,
which become the members of the argument object, or one aggregate, which is the
argument object. Use an aggregate to describe or constrain arguments:
annotations on parameters themselves are a compile error.

**Return values.** A tool can return any supported type, a `Result` of one,
`void`, or `Status`. `void` and a successful `Status` return `{}`. To refuse a
call with a message for the model, return `std::unexpected(tool_error("..."))`.

**Toolboxes.** `bind(std::shared_ptr<T>)` binds the tool member functions of
an object. Each handler keeps the object alive:

```cpp
class Counter {
public:
  [[= scrydantic::tool{"Increment the counter"}]] int increment() { return ++count_; }

private:
  int count_{};
};

auto counter_tools = scrydantic::bind(std::make_shared<Counter>());
```

**Call context.** To pass the host's own data to each call, such as the current
user, give the binding a context type. A tool takes it as a leading
`const Context&` parameter, which is not part of the input schema, and `invoke`
takes the context first:

```cpp
struct Session { std::string user; };

namespace session_tools {
[[= scrydantic::tool{"Say who is calling"}]]
std::string whoami(const Session& session) { return session.user; }
}

auto tools = scrydantic::bind<^^session_tools, Session>();
tools[0].invoke(Session{"ada"}, scrydantic::Json{.text = "{}"}); // "ada"
```

**Dispatching by name.** `ToolSet<Context>::make(tools)` gathers bound tools,
such as the results of several `bind` calls, so a host can call them by the name
the model used. It fails if two tools share a name. A name the set does not
hold gets a refusal that lists the names it does:

```cpp
auto set = scrydantic::ToolSet<>::make(scrydantic::bind<^^weather>());
set->invoke("forecast", scrydantic::Json{.text = R"({"city":"Oslo","days":2})"});
set->invoke("fly", scrydantic::Json{.text = "{}"}).error().model_message;
// unknown tool "fly"; known tools: forecast

set->find("forecast")->description; // Get the forecast for a city
set->tools();                       // every tool, in the order given, to list
```

**Without annotations.** `bind_handler<Args>(callable)` turns any callable
that takes an `Args` aggregate into a handler, with `input_schema_v<Args>` as
its schema.

**Without handlers.** `describe<^^ns>()`, or `describe<Toolbox>()` for a
class, returns the name, description, and input schema of each tool as
compile-time constants, for a manifest that needs no handlers.

## Working with raw JSON

- `Json` holds JSON text in its `text` member. Making one does not parse it.
- `JsonView::parse(json)` parses text into a read-only view, with `kind()`,
  `find(key)`, `at(index)`, `key_at(index)`, `string()`, `number()`,
  `boolean()`, and `to_json()`. Object keys are visited in sorted order.
- `is_valid_json(text)` checks text without building a document.
- `escape_json_string(text)` and `format_json_number(value)` spell a string or
  a number exactly as the encoder does, for JSON built by hand.
- `keys_v<T>` lists the JSON keys of an aggregate, in encoded order, and
  `tags_v<V>` lists the tags of a variant's alternatives.

## Compile-time errors

A type that Scrydantic cannot handle fails with one `static_assert` that names
the member and the reason, rather than pages of template errors. For example,
encoding an `int*` says:

```text
int* does not satisfy scrydantic::Encodable: int* is not a supported reflected value
```

The concepts `SupportedValue`, `Encodable`, `Decodable`, `ToolArguments`,
`ToolHandlerFor`, and `Toolbox` test the same rules without failing. If you
write a library on top of Scrydantic, `<scrydantic/diagnostics.hpp>` gives you
the reason as text, such as `encodable_diagnostic<T>()`, so that your own
functions can fail the same way.
