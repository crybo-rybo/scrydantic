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
- [For library authors](#for-library-authors)

## Models

A model is a plain aggregate: a struct with public, named members and no
constructors of its own. Its members can be any of these types:

| C++ type | JSON |
|---|---|
| `bool` | `true` or `false` |
| integers, such as `int` or `std::uint64_t` | an integer within the type's range |
| `float`, `double` | a finite number |
| `std::string` | a string |
| a scoped `enum class` | the enumerator's name, as a string: its identifier, or the `name` annotation it carries |
| `std::optional<T>` | `T` or `null` |
| `std::vector<T>` (not `vector<bool>`) | an array |
| `std::array<T, N>` | an array of exactly `N` elements |
| `std::map<std::string, T>`, `std::unordered_map<std::string, T>` | an object with any keys, each value a `T` |
| `std::indirect<T>` | whatever `T` is |
| another aggregate | an object |
| `std::variant<A, B, ...>` of tagged aggregates | an object with a `"type"` member |
| `scrydantic::Json` | any JSON value, held as its canonical text |

`std::int8_t` and `std::uint8_t` are character types in C++, so they are
rejected. Use `std::int16_t`. `char` and the other character types are rejected
too. Encoding also accepts `std::string_view` and reference members, which
decoding cannot fill and which have no schema.

A decoded `Json` member holds the canonical text of its value, not the input's
own bytes. Keys are sorted, whitespace is dropped, and each number is read as a
64-bit integer or a `double`, so a number that neither holds exactly loses
digits:

```cpp
struct Message { scrydantic::Json payload; };

auto message = scrydantic::decode<Message>(
    R"({"payload":{"id":123456789012345678901234,"b":1,"a":2.50}})");
message->payload.text; // {"a":2.5,"b":1,"id":1.2345678901234569E23}
```

Keeping the input's own text instead is tracked in
[issue #28](https://github.com/crybo-rybo/scrydantic/issues/28).

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

**Maps.** Use a map for an object whose keys are data, not member names:

```cpp
struct Scores { std::map<std::string, int> by_player; };

scrydantic::encode(Scores{{{"zed", 1}, {"amy", 2}}})->text;
// {"by_player":{"amy":2,"zed":1}}

scrydantic::decode<Scores>(R"({"by_player":{"amy":"two","a b":1.5}})")
    .error().message;
// $.by_player["a b"] must be an integer
// $.by_player.amy must be an integer
```

The key type must be `std::string`. The mapped type can be any type from the
table. A custom comparator, hash, or allocator is fine. Encoding writes the
keys in sorted order whatever order the map keeps them in, so an
`unordered_map` is sorted each time it is written. If the input repeats a key,
the last one wins. `std::multimap` is rejected, and so are constraints on a
map: `items` on one is a compile error.

**Recursion.** A model can contain itself through `std::vector<T>`, a map, or
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
| `title{"..."}` | member, class | Sets the `title` of the member's or the class's schema. It is written to the schema only. |
| `keyword{"key", "json"}`, `keyword_string{"key", "text"}` | member, class | Adds a keyword of your choosing, such as `format` or `examples`, to the member's or the class's schema. It is written to the schema only. Decoding does not check it. |
| `name{"..."}` | member, enumerator, tool | Replaces the member's JSON key, the enumerator's JSON string, or the tool's name. |
| `rename_all{rule}` | class, enum | Derives the JSON key of every member, or the JSON string of every enumerator, from its identifier, by `camel_case`, `pascal_case`, `kebab_case`, or `screaming_snake_case`. A `name` still wins. |
| `tag{"..."}` | class | The class's `"type"` value when it is a variant alternative. |
| `skip_null` | class, optional member | Leaves out an empty optional instead of writing `null`, and lets the input leave it out. On a class, it applies to every optional member. |
| `emit_null` | optional member | Writes `null` again for one member of a `skip_null` class. |
| `ignore_unknown` | class | Decoding ignores members the class does not declare, instead of rejecting them. |
| `flatten` | aggregate member | Makes the member's fields fields of the enclosing object. The member has no key of its own. |
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

**Naming rules.** Where the JSON spells its keys in another convention than
the C++ does, `rename_all` on the class derives every key from the member's
identifier, and only the exceptions need a `name`:

```cpp
struct [[= scrydantic::rename_all{scrydantic::camel_case}]] Tool {
  std::string name;
  scrydantic::Json input_schema;                          // "inputSchema"
  std::optional<bool> read_only_hint;                     // "readOnlyHint"
  [[= scrydantic::name{"_meta"}]] scrydantic::Json meta;  // a name still wins
};

Tool tool{
    .name = "search", .input_schema = {"{}"}, .read_only_hint = true, .meta = {"{}"}};
scrydantic::encode(tool)->text;
// {"_meta":{},"inputSchema":{},"name":"search","readOnlyHint":true}
scrydantic::decode<Tool>(
    R"({"_meta":{},"input_schema":{},"name":"search","readOnlyHint":true})")
    .error().model_message;
// $ contains unknown member "input_schema"
// $.inputSchema is a required member
```

The derived key is the member's key everywhere: in the schema, in encoding and
decoding, in issue paths, and in `keys_v`. On a scoped enum the same annotation
derives the enumerators' strings:

```cpp
enum class [[= scrydantic::rename_all{scrydantic::screaming_snake_case}]] Level {
  low_water,
  high_water,
  off,
};

scrydantic::schema_v<Level>;
// {"enum":["LOW_WATER","HIGH_WATER","OFF"],"type":"string"}
```

A rule reads an identifier as words separated by underscores:

| Rule | `read_only_hint` | `default_` | `http2_enabled` |
|---|---|---|---|
| `camel_case` | `readOnlyHint` | `default` | `http2Enabled` |
| `pascal_case` | `ReadOnlyHint` | `Default` | `Http2Enabled` |
| `kebab_case` | `read-only-hint` | `default` | `http2-enabled` |
| `screaming_snake_case` | `READ_ONLY_HINT` | `DEFAULT` | `HTTP2_ENABLED` |

Trailing underscores are dropped, so a member named around a keyword gets the
keyword as its key. Leading underscores are kept (`_meta` stays `_meta` under
`camel_case`), and several underscores between two words separate them once. A
capital letter starts no word: `camel_case` and `pascal_case` set each word's
first letter and keep the rest as written, so under `camel_case` `userID` stays
`userID` and `HTTPServer` becomes `hTTPServer`, while `kebab_case` and
`screaming_snake_case` set every letter. Where that is not the key you want,
give the member a `name`.

The rule covers what the class or enum itself declares. A member's type states
its own rule or has none, so a class has the same JSON wherever it is used, and
a field that comes from a flattened member follows the class that declares it.
The keys of a map, a variant alternative's `tag`, and the arguments that a
tool's parameter list names are never renamed. Two members that end with the
same key do not compile, with or without a rule, and neither do two rules on
one class or enum, or a rule on a member, an enumerator, or a tool function.

**Enumerators.** An enumerator is written as its identifier, or as its enum's
`rename_all` spells that, unless it carries a `name`, which gives an enum a
value that is not a C++ identifier: a keyword, a value with punctuation, or one
in another case convention. The annotation goes after the enumerator's
identifier:

```cpp
enum class Visibility {
  public_ [[= scrydantic::name{"public"}]],
  private_ [[= scrydantic::name{"private"}]],
  internal,
};
struct Post { Visibility visibility; };

scrydantic::schema_v<Visibility>;
// {"enum":["public","private","internal"],"type":"string"}
scrydantic::encode(Post{Visibility::public_})->text;
// {"visibility":"public"}
scrydantic::decode<Post>(R"({"visibility":"public_"})").error().model_message;
// $.visibility is not a declared enumerator; must be one of: public, private, internal
```

An enum with one enumerator is a constant:
`enum class Version { v2 [[= scrydantic::name{"2.0"}]] };` has the schema
`{"enum":["2.0"],"type":"string"}`, and a member of that type decodes from
`"2.0"` and nothing else. Two enumerators of one enum cannot end with the same
name, and `name` is the only Scrydantic annotation an enumerator takes; either
mistake is a compile-time error.

**Variants.** Every alternative of a `std::variant` must be an aggregate with a
distinct `tag`. The encoded object carries the tag under the key `"type"`, so an
alternative cannot have a member whose key is `"type"`.

```cpp
struct [[= scrydantic::tag{"circle"}]] Circle { double radius; };
struct [[= scrydantic::tag{"square"}]] Square { double side; };
using Shape = std::variant<Circle, Square>;
// Shape{Circle{1.5}} encodes as {"radius":1.5,"type":"circle"}
```

**Flattened members.** `flatten` gives an object the fields of one of its
members, so that types which share a set of fields can share its declaration:

```cpp
struct Tool { std::string name; };
struct Page { std::optional<std::string> next_cursor; };

struct ToolList {
  std::vector<Tool> tools;
  [[= scrydantic::flatten]] Page page;
};

ToolList list{.tools = {{.name = "search"}}, .page = {.next_cursor = "abc"}};
scrydantic::encode(list)->text;
// {"next_cursor":"abc","tools":[{"name":"search"}]}
```

`next_cursor` is a field of `ToolList` everywhere: in the schema's `properties`
and `required`, in decoding, where an issue in it is reported at
`$.next_cursor`, and in `keys_v<ToolList>`. A flattened class may flatten
members of its own.

Each field follows the class that declares it. A `skip_null` on `Page` applies
to `next_cursor`, and a `check` on `Page` runs on `list.page` once its fields
have decoded. Unknown members belong to the whole object, so `ignore_unknown`
is read from the outermost class alone. A default member initializer on the
flattened member makes every field under it optional.

The member must be an aggregate itself, not a `std::optional` or a
`std::indirect` of one. It cannot carry `name`, `description`, or `check`,
because it has no key or value of its own, and two fields of one object cannot
share a key.

## Schemas

`schema_v<T>` is the JSON Schema of `T`, as a `constexpr std::string_view`:

```cpp
scrydantic::schema_v<Reading>
// {"additionalProperties":false,
//  "properties":{"degrees":{"type":"number"},
//                "label":{"anyOf":[{"type":"string"},{"type":"null"}]},
//                "unit":{"default":"celsius",
//                        "enum":["celsius","fahrenheit"],"type":"string"}},
//  "required":["degrees","label"],"type":"object"}
```

(The real string has no whitespace.) The schemas use a portable subset of JSON
Schema that model APIs accept: objects are closed, optionals and variants use
`anyOf`, and integers carry their type's `minimum` and `maximum`.

A map is the one open object. Its schema allows any key, and gives the schema
of every value as `additionalProperties`:

```cpp
scrydantic::schema_v<Scores>
// {"additionalProperties":false,
//  "properties":{"by_player":{
//    "additionalProperties":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},
//    "type":"object"}},
//  "required":["by_player"],"type":"object"}
```

**Defaults.** A member with a default member initializer has a `default` in
its schema: the value the member has when the input leaves it out, written as
`encode` writes it. `unit` above is one. An optional member with an initializer
states its default beside the `anyOf`, and an empty one states
`"default":null`.

The compiler reads the value from a default-constructed object of the class.
Where it cannot, the schema has no `default` for the member and is otherwise the
same:

- An initializer of the class is not a constant expression, such as a call to a
  function that is not `constexpr`. Then no member of that class has a
  `default`.
- `encode` would refuse the value (a NaN, an enum value that is not an
  enumerator, a string that is not UTF-8), or the member is a `Json`.
- The value is a `float` that is not exactly a decimal of six digits or fewer,
  or a `double` from 2^53 up to 2^64. `0.5f` and `100.0f` have a default and
  `0.1f` does not, because a schema cannot find at compile time the digits
  `encode` writes for it. Use a `double` for such a default.

A member that `skip_null` applies to has no `default`. Its absence decodes as
an empty optional, whatever its initializer.

A default is the program's own value, so it is not checked against the
member's constraints, as decoding does not check an absent member:
`[[= scrydantic::range{.min = 1}]] int count{};` has `"default":0` beside
`"minimum":1`. Give such a member an initializer within its range, or none,
which makes it required.

**Titles.** `title{"..."}` gives a member's schema, or a class's, a `title`: a
label that a form or generated documentation shows. Scrydantic never makes one
up from an identifier.

```cpp
struct [[= scrydantic::title{"Postal address"}]] Address {
  [[= scrydantic::title{"Postcode"}]] std::string code;
};

scrydantic::schema_v<Address>
// {"additionalProperties":false,
//  "properties":{"code":{"title":"Postcode","type":"string"}},
//  "required":["code"],"title":"Postal address","type":"object"}
```

A class's title is on its schema wherever the class appears. A member of that
type with a title of its own shows the member's instead. On a `std::optional`
member a title goes with the value's schema, inside the `anyOf`, as a
constraint does. An optional variant has no schema of its own inside the
`anyOf`, so its title is beside it.

**Custom keywords.** `keyword{"key", "json"}` adds any other keyword to a
member's schema, or a class's: `format`, `examples`, `deprecated`, or a vendor
extension under `x-`. Its value is JSON text. `keyword_string{"key", "text"}`
is the same for a value that is a string, and takes the text as it is:

```cpp
struct Query {
  [[= scrydantic::keyword_string{"format", "date-time"}]] std::string since;
  [[= scrydantic::keyword{"examples", R"(["us-west1","eu-north1"])"}]] std::string zone;
  [[= scrydantic::keyword{"x-header", R"("Region")"}]] std::string region;
};

scrydantic::schema_v<Query>
// {"additionalProperties":false,
//  "properties":{"region":{"type":"string","x-header":"Region"},
//                "since":{"format":"date-time","type":"string"},
//                "zone":{"examples":["us-west1","eu-north1"],"type":"string"}},
//  "required":["region","since","zone"],"type":"object"}
```

Like `pattern`, a custom keyword is written to the schema only. Decoding does
not check it: a `since` that is not a date decodes. If you rely on a keyword,
check it in a validator too.

The value must be exactly one JSON value, or the type does not compile. It is
written as you wrote it, without the whitespace between its tokens. The keys of
an object in it are not sorted, and its numbers are not respelled, so write the
value in canonical form if the whole schema must be canonical. The keyword
itself is sorted among the schema's other keywords.

A member or a class can carry several keywords, each with a key of its own. A
key that Scrydantic writes itself is a compile error on any member or class:
`type`, `enum`, `anyOf`, `items`, `properties`, `required`,
`additionalProperties`, `$ref`, `$defs`, `description`, `title`, `default`, and
the constraint keywords. A custom keyword goes where a title goes. A class's
keyword is on its schema wherever the class appears, unless a member of that
type sets the same key.

To give a library of your own an annotation that takes a plain name, wrap
`keyword_string` in a `consteval` function:

```cpp
namespace http {
template <std::size_t N> consteval auto header(const char (&name)[N]) {
  return scrydantic::keyword_string{"x-header", name};
}
}

struct Routed {
  [[= http::header("Region")]] std::string region;
};
// "region":{"type":"string","x-header":"Region"}
```

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

A member whose type is recursive states its own `description`, `default`,
`title`, and custom keywords beside the `$ref`. JSON Schema 2019-09 and later
read a keyword there, and draft-07 ignores it.

A `Json` can hold any JSON value, so its schema is the empty schema `{}`, which
accepts every value. It has one wherever it sits: as a member, an element, a
tool parameter or result, or the root. A constraint on a `Json` member is a
compile error.

```cpp
struct Payload { scrydantic::Json data; };

scrydantic::schema_v<Payload>
// {"additionalProperties":false,"properties":{"data":{}},
//  "required":["data"],"type":"object"}
scrydantic::schema_v<std::optional<scrydantic::Json>> // {"anyOf":[{},{"type":"null"}]}
scrydantic::schema_v<std::vector<scrydantic::Json>>   // {"items":{},"type":"array"}
```

`input_schema_v<Args>` is the schema of a tool's argument object. It is the
same as `schema_v<Args>`, but `Args` must be an aggregate. A map cannot be the
argument object, but it can be a member of one.

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

`encode` re-spells a `Json` member canonically, as decoding does, so a number
in it that a `double` cannot hold exactly loses digits. `encode_verbatim(value)`
is the same, except that a `Json` member is copied byte for byte. Use it when
your program builds a payload with such numbers. It cannot bring back digits
that decoding already lost. Tool results are encoded this way.

## Decoding and errors

`decode<T>(text)` parses JSON text and returns a `Result<T>`. The text can be
a string literal, a `std::string`, a `std::string_view`, or a `Json`. It also
accepts a `JsonView` that you have already parsed.

```cpp
auto reading = scrydantic::decode<Reading>(R"({"degrees":3,"label":null})");
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
auto drawing = scrydantic::decode<Drawing>(
    R"({"shapes":[{"type":"circle","radius":"big"},{"type":"hex"}],"extra":1})");
drawing.error().message;
// $ contains unknown member "extra"
// $.shapes[0].radius must be a number
// $.shapes[1].type must be one of: circle, square
```

Text that is not JSON at all is one `IssueKind::invalid_json` issue at `$`.
The message says why and where reading stopped. Lines and columns count from
1, and a column counts characters (code points), not bytes:

```cpp
auto broken = scrydantic::decode<Reading>(R"({"degrees" 3})");
broken.error().message;
// JSON text is not valid JSON: expected ':' at line 1, column 12
```

Tool arguments that are not JSON fail the same way, as
`tool arguments are not valid JSON: ...`, and so does `JsonView::parse`, as
`JSON text is not valid: ...`. Its documentation lists the reasons.

**Decoding text-only sources.** Some sources carry every value as text: the
variables of a URL, the fields of a form, environment variables, command-line
arguments. Others send a nested value as JSON text inside a string. Strict
decoding rejects both, so `decode` takes a `DecodeOptions` for them. Both
options are off by default:

```cpp
const scrydantic::DecodeOptions options{.coerce_scalars = true};
auto reading = scrydantic::decode<Reading>(
    R"({"degrees":"21.5","label":null})", options);
reading->degrees; // 21.5
```

- `coerce_scalars` accepts a string where a `bool` or a number is required,
  when its whole text is `true`, `false`, or a JSON number, with no whitespace
  around it.
- `coerce_structures` accepts a string where an array or an object is required,
  when its text is JSON of that kind: `"[1,2]"` for a `std::vector<int>`, or
  `"{\"city\":\"Oslo\"}"` for an aggregate.

The value is then decoded as if it had been written in the string's place.
`"5.0"` is not an integer, `"70000"` is out of range for a `std::int16_t`, and
the member's constraints and validators apply. An issue has the path and the
reason it has in a strict decode, such as `$.degrees must be a number` for
`"warm"`, and an issue inside a coerced structure continues the path of its
string. The text of a coerced structure counts toward the nesting limit from
where its string stands: the arrays and objects around the string and the
nesting of the text may add up to 256 (`max_json_depth`).

Nothing else is converted. A number is not accepted for a string or for a
`bool`, `"null"` and `""` are not `null`, and a `std::string` or `Json` member
takes its string as it is. Schemas do not change: a schema says what a producer
should send, and the options say what your program tolerates.

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
decoded. Return `rejection(model_text, host_text)` to reject a value. It is the
same function as `tool_error()`, the name a tool uses to refuse a call. The model
sees only `model_text`, and your logs get `host_text`:

```cpp
inline scrydantic::Status known_city(const std::string& city) {
  if (city == "Oslo" || city == "Lima") {
    return {};
  }
  return std::unexpected(
      scrydantic::rejection("must be a known city", "has no row in cities"));
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

Each `BoundTool` has a `name`, a `description`, an `input_schema`, an
`output_schema`, and an `invoke` handler. The handler takes the arguments as
`Json` and returns a `Result<Json>`:

```cpp
tools[0].input_schema.text;
// {"additionalProperties":false,
//  "properties":{"city":{"type":"string"},
//                "days":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},
//  "required":["city","days"],"type":"object"}
tools[0].output_schema->text; // the same text as scrydantic::schema_v<Reading>

auto result = tools[0].invoke(scrydantic::Json{.text = R"({"city":"Oslo"})"});
result.error().model_message; // $.days is a required member
```

**Parameters.** A tool can take no parameters, any list of named parameters,
which become the members of the argument object, or one aggregate, which is the
argument object. Use an aggregate to describe or constrain arguments:
annotations on parameters themselves are a compile error. A `Json` parameter
takes any JSON value, for a tool that stores or forwards a document without
reading it. Its schema is `{}`, and the tool receives the value's canonical
text:

```cpp
namespace documents {
[[= scrydantic::tool{"Store a document"}]]
std::string store(scrydantic::Json document) { return document.text; }
}

auto stored = scrydantic::bind<^^documents>();
stored[0].input_schema.text;
// {"additionalProperties":false,"properties":{"document":{}},
//  "required":["document"],"type":"object"}
stored[0].invoke(scrydantic::Json{.text = R"({"document":{"b":[1, 2.50],"a":null}})"})->text;
// "{\"a\":null,\"b\":[1,2.5]}"
```

A parameter's default argument is never published as a `default`, because
reflection cannot read its value. To publish one, take an aggregate and give its
member an initializer.

**Return values.** A tool can return any supported type, a `Result` of one,
`void`, or `Status`. `void` and a successful `Status` return `{}`. A returned
`Json`, alone or as a member, is checked and then sent as the tool wrote it. To
refuse a call with a message for the model, return
`std::unexpected(tool_error("..."))`.

A tool that returns `T` or `Result<T>` publishes `schema_v<T>` as its
`output_schema`, for a host that lists what its tools return beside what they
take. It is a `std::optional<Json>`, and it is empty for a tool that returns
`void` or `Status`: the `{}` such a tool answers with says only that the call
succeeded. A tool that returns `Json` has the output schema `{}`. The handler
does not check a result against its schema.

**Failures.** `invoke` returns the same `Error` type for three kinds of
failure. A host tells them apart by `issues` and `model_message`:

| Failure | `issues` | `model_message` |
|---|---|---|
| The arguments did not decode, or a validator rejected them. | not empty | describes the issues |
| The tool refused the call with `tool_error()`. | empty | the tool's text for the model |
| The result could not be encoded. This is a host-side bug. | empty | empty |

`message` has the host's text. For example:

```cpp
namespace notes {
[[= scrydantic::tool{"Read a note"}]]
scrydantic::Result<std::string> read(std::int32_t id) {
  if (id == 0) {
    return std::unexpected(
        scrydantic::tool_error("no note has id 0", "notes table has no row 0"));
  }
  return std::string{"\xC3"}; // a stored note cut off inside a character
}
}
```

```text
arguments     issues  model_message            message
{"id":"one"}  1       $.id must be an integer  $.id must be an integer
{"id":0}      0       no note has id 0         notes table has no row 0
{"id":1}      0                                the value holds a string that is not valid UTF-8, so it cannot be JSON
```

An MCP-style host sets `isError` for all three. It returns the model's text
for the first two, and logs the third as a failure of its own:

```cpp
struct Reply { std::string text; bool is_error{}; };

Reply call(const scrydantic::BoundTool<>& tool, scrydantic::Json arguments) {
  auto result = tool.invoke(std::move(arguments));
  if (result) {
    return {.text = result->text};
  }
  const scrydantic::Error& error = result.error();
  if (!error.issues.empty()) {
    // Bad arguments: the model can correct them and call again.
    return {.text = error.model_message, .is_error = true};
  }
  if (!error.model_message.empty()) {
    // The tool refused. `message` can say more than the model is told.
    std::clog << "refused: " << error.message << '\n';
    return {.text = error.model_message, .is_error = true};
  }
  // The result could not be encoded.
  std::cerr << "tool failed: " << error.message << '\n';
  return {.text = "the tool failed", .is_error = true};
}
```

`invoke` returns a tool's own `Error` unchanged, so the table holds only when a
tool refuses with `tool_error()` and gives the model some text. If a tool returns
the error from its own `decode` call, it looks like bad arguments. If it calls
`tool_error("")`, it looks like an encoding failure. To avoid this, wrap the
error in `tool_error()` with text of your own.

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
the model used. It fails if two tools share a name, or if one has no handler.
A name the set does not hold gets a refusal that lists the names it does, which
`unknown_tool_error(name, known)` builds for a host that keeps its own table:

```cpp
auto set = scrydantic::ToolSet<>::make(scrydantic::bind<^^weather>());
set->invoke("forecast", scrydantic::Json{.text = R"({"city":"Oslo","days":2})"});
set->invoke("fly", scrydantic::Json{.text = "{}"}).error().model_message;
// unknown tool "fly"; known tools: forecast

set->find("forecast")->description; // Get the forecast for a city
set->tools();                       // every tool, in the order given, to list
```

A set does not change once it is made. To add or remove tools, take them back
with `release()`, which leaves the set empty, and make a new set from them.

Calling a tool does not change the set either. A handler can be called through
a `const` reference, so a `const` set, or a `const` vector of bound tools, can
serve calls, and several threads can share one. Concurrent calls are then as
safe as the tools' own code: a toolbox whose tools change it needs its own lock.

**Without annotations.** `bind_handler<Args>(callable)` turns any callable
that takes an `Args` aggregate into a handler, with `input_schema_v<Args>` as
its schema. The handler calls it as `const`, so a `mutable` lambda, or a functor
whose `operator()` is not `const`, is a compile error. Keep state that calls
change outside the callable, in a toolbox or behind a pointer it captures:

```cpp
struct Ticket { std::string title; };

auto next_id = std::make_shared<std::atomic<std::int64_t>>(1);
const auto open_ticket = scrydantic::bind_handler<Ticket>(
    [next_id](Ticket) { return next_id->fetch_add(1); });
open_ticket(scrydantic::Json{.text = R"({"title":"Fix the lamp"})"})->text; // 1
open_ticket(scrydantic::Json{.text = R"({"title":"Oil the door"})"})->text; // 2
```

`bind_handler` returns the handler alone, so it carries no schema. To list such
a tool, build the `BoundTool` yourself. `output_schema` may stay empty, or take
the schema of what the callable returns:

```cpp
using scrydantic::Json;

scrydantic::BoundTool<> tool{
    .name = "open_ticket",
    .description = "Open a ticket",
    .input_schema = Json{.text = std::string{scrydantic::input_schema_v<Ticket>}},
    .output_schema = Json{.text = std::string{scrydantic::schema_v<std::int64_t>}},
    .invoke = scrydantic::bind_handler<Ticket>(
        [next_id](Ticket) { return next_id->fetch_add(1); }),
};
```

**Without handlers.** `describe<^^ns>()`, or `describe<Toolbox>()` for a
class, returns the name, description, input schema, and output schema of each
tool as compile-time constants, for a manifest that needs no handlers. A
descriptor's `output_schema` is a `std::string_view`, empty for a tool that
returns no value:

```cpp
static constexpr auto described = scrydantic::describe<^^weather>();
static_assert(described[0].output_schema == scrydantic::schema_v<Reading>);
```

**Without `tool`.** To call any function with JSON arguments and keep its typed
result, see [For library authors](#for-library-authors).

## Working with raw JSON

- `Json` holds JSON text in its `text` member. Making one does not parse it.
- `JsonView::parse(text)` parses text into a read-only view, with `kind()`,
  `find(key)`, `at(index)`, `key_at(index)`, `string()`, `number()`,
  `boolean()`, and `to_json()`. Object keys are visited in sorted order. The
  view holds its own copy of every key and string, so the text need not
  outlive it. `depth()` is how deep the value nests; the parser and the encoder
  accept at most `max_json_depth`.
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

## For library authors

A library on top of Scrydantic may call functions with JSON arguments by rules
of its own. A command, a route, or a remote procedure is not a tool with a
description for a model, and what it returns may not become JSON at all. The
steps that `bind` is made of are public for this, and none of them needs a
`tool` annotation:

- `arguments_t<^^fn>` is the argument object of a function, formed from its
  parameters as **Parameters** describes for a tool. It is an ordinary reflected
  type: `input_schema_v` gives its schema, `decode` reads it, and `keys_v`
  lists its keys.
- `apply<^^fn>(std::move(arguments))` calls the function with that object and
  returns exactly what the function returns: any type, including `void`, a
  reference, a `Result`, and a type that Scrydantic cannot encode.
- `arguments_diagnostic<^^fn>("mylib::add")` is the reason that a function's
  parameters cannot form an argument object, as text that starts with the name
  of your own function. It is empty when they can.
- `annotated_functions_of(^^scope, ^^annotation)` lists the functions of a
  namespace or a class that carry your own annotation, in declaration order.

Here a small library runs a function as a command. It reads the schema at
compile time, decodes the arguments from a `JsonView`, calls the function, and
gets its typed result, a duration, which it prints in its own way:

```cpp
namespace shell {

struct command {}; // this library's own annotation

template <std::meta::info Function,
          typename Binding = typename[:scrydantic::binding_of(Function, ^^void):]>
std::string run(const scrydantic::JsonView& input) {
  constexpr std::string_view problem =
      scrydantic::arguments_diagnostic<Function>("shell::run");
  static_assert(problem.empty(), problem);
  if constexpr (!problem.empty()) {
    return {};
  } else {
    using Arguments = scrydantic::arguments_t<Function, void, Binding>;
    static constexpr std::string_view schema = scrydantic::input_schema_v<Arguments>;

    auto arguments = scrydantic::decode<Arguments>(input);
    if (!arguments) {
      return arguments.error().message + "; expected " + std::string{schema};
    }
    const std::chrono::seconds left =
        scrydantic::apply<Function, void, Binding>(std::move(*arguments));
    return std::to_string(left.count()) + " s";
  }
}

} // namespace shell

namespace timers {
[[= shell::command{}]]
std::chrono::seconds remaining(std::string name, std::int32_t laps);

[[= shell::command{}]]
std::chrono::seconds snooze();
}

const auto input = scrydantic::JsonView::parse(R"({"name":"tea","laps":2})");
shell::run<^^timers::remaining>(*input); // 180 s

const auto short_input = scrydantic::JsonView::parse(R"({"name":"tea"})");
shell::run<^^timers::remaining>(*short_input);
// $.laps is a required member; expected {"additionalProperties":false,
//  "properties":{"laps":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},
//                "name":{"type":"string"}},
//  "required":["laps","name"],"type":"object"}
```

A function that cannot be bound fails once, under the library's name:

```text
shell::run<^^timers::reset>(): timers::reset has parameter `clock`: timers::Clock* is not a supported reflected value
```

**The `Binding` parameter.** A function's parameter names come from the
declarations that the calling translation unit has seen, and `binding_of`
records them in a type. `arguments_t` and `apply` take that type as the template
parameter after the context, and compute it by default. A template of your own,
like `run` above, must compute the same default on its own parameter list and
pass it on. Then two translation units that declare one function with different
parameter names each get their own code. `bind` has a `Tools` parameter for the
same reason, with `tools_of` and `toolbox_tools_of` as its defaults.

**Context and objects.** A call context is the second template argument, as
for `bind`, and goes first in the call. A non-static member function is called
on an object, which goes before the context. An explicit object parameter is
initialized from that object:

```cpp
scrydantic::apply<^^greet, Session>(session, std::move(*arguments));
scrydantic::apply<^^Timer::lap, Session>(timer, session, std::move(*arguments));
```

**Finding your functions.** `annotated_functions_of` is the walk that `bind`
finds tools with. It skips the templates and namespace aliases in a namespace,
on which `std::meta::annotations_of` throws:

```cpp
template for (constexpr auto function : std::define_static_array(
                  scrydantic::annotated_functions_of(^^timers, ^^shell::command))) {
  std::cout << std::meta::identifier_of(function) << ' '
            << scrydantic::keys_v<scrydantic::arguments_t<function>>.size() << '\n';
}
// remaining 2
// snooze 0
```
