// fields_v, field_members_v, and enumerators_v: what the codec and the schema
// already hold about an object's fields and an enum's enumerators, published
// for a program that describes a type in a form of its own. The tests state
// each view for every way a field gets its key, its required flag, and its
// schema, and prove that a field's schema is the text its object's schema
// holds under the key.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <meta>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <scrydantic/tools.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace field_metadata {

using scrydantic::FieldDescriptor;

// ---- The types ---------------------------------------------------------------

inline scrydantic::Status known_city(const std::string& city) {
  if (city == "Oslo" || city == "Lima") {
    return {};
  }
  return std::unexpected(
      scrydantic::rejection("must be a known city", "has no row in cities"));
}

// The guide's Booking: two required members and one with an initializer.
struct Booking {
  [[= scrydantic::check<&known_city>]] std::string city;
  [[= scrydantic::range{.min = 1, .max = 8}]] std::int32_t guests;
  [[= scrydantic::length{.max = 200}]] std::string note{};
};

// Another library's annotation, which Scrydantic does not read.
struct column {
  int index{};
};

// A renamed member stands where its key sorts, and members carry annotations
// that only the other library reads.
struct Row {
  [[= column{2}]] std::string title;
  [[ = column{1}, = scrydantic::name{"id"} ]] std::int64_t identifier{};
  std::optional<double> weight;
};

// Declared out of alphabetical order, which is the order the names are listed in.
enum class Sky { overcast, clear };

enum class Channel {
  stable,
  pre_release[[= scrydantic::name{"pre-release"}]],
  nightly,
};

enum class[[= scrydantic::rename_all{scrydantic::kebab_case}]] Mode {
  read_only,
  read_write[[= scrydantic::name{"rw"}]],
  append_only,
};

// skip_null on the class: an optional member may be left out, but for the one
// that emit_null takes back.
struct[[= scrydantic::skip_null]] Patch {
  std::optional<std::string> title;
  [[= scrydantic::emit_null]] std::optional<std::int32_t> priority;
  [[= scrydantic::description{"Row to change"}]] std::string id;
  std::int32_t revision{1};
};

// A class that contains itself: as a root it refers to itself as "#".
struct Category {
  std::string name;
  [[= scrydantic::description{"Narrower categories"}]] std::vector<Category> children;
};

// A class that holds a recursive one, which its schema defines under "$defs".
struct Shelf {
  [[= scrydantic::description{"Where it starts"}]] Category root;
  std::optional<Category> spare;
  std::string label;
};

struct Page {
  [[= scrydantic::description{"Opaque cursor"}]] std::optional<std::string> cursor;
  std::int32_t limit{50};
};

// The fields of a flattened member are fields of the object that holds it.
struct Listing {
  std::string query;
  [[= scrydantic::flatten]] Page page;
};

// Every keyword a member's schema can carry beside its type: a title, custom
// keywords, a constraint, and a default, under keys that a class rule spells.
struct[[= scrydantic::rename_all{scrydantic::camel_case}]] Event {
  [[
    = scrydantic::title{"Starts"},
    = scrydantic::keyword_string{"format", "date"}
  ]] std::string starts_on;
  [[
    = scrydantic::keyword{"uniqueItems", "true"}, = scrydantic::items{.max = 3}
  ]] std::vector<std::string>
      tag_names{};
  Sky sky{Sky::clear};
};

// Encoding alone accepts a std::string_view member, so the type has no schema.
struct Borrowed {
  std::string_view text;
  std::int32_t count{};
};

// A tagged alternative's tag is carried under "type", and is no member.
struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};
};

// A function whose argument object is listed: an annotated parameter and one
// with a default argument.
inline std::string
plan([[= scrydantic::description{"City name"}]] std::string city,
     [[= scrydantic::range{.min = 1, .max = 14}]] std::int32_t days = 1) {
  return city + ":" + std::to_string(days);
}

using PlanArguments = scrydantic::arguments_t<^^plan>;

// A function without parameters has an argument object without fields.
inline std::int32_t ping() { return 1; }

using PingArguments = scrydantic::arguments_t<^^ping>;

// ---- Helpers -----------------------------------------------------------------

constexpr bool same(const FieldDescriptor& field, const FieldDescriptor& expected) {
  return field.key == expected.key && field.description == expected.description &&
         field.required == expected.required && field.schema == expected.schema;
}

template <typename Type>
constexpr bool fields_are(const std::vector<FieldDescriptor>& expected) {
  const auto& fields = scrydantic::fields_v<Type>;
  if (fields.size() != expected.size()) {
    return false;
  }
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (!same(fields[index], expected[index])) {
      return false;
    }
  }
  return true;
}

template <typename Enum>
constexpr bool names_are(const std::vector<std::string_view>& expected) {
  const auto& names = scrydantic::enumerators_v<Enum>;
  return std::vector<std::string_view>{names.begin(), names.end()} == expected;
}

// The `properties` and `required` members of an object's schema, written from
// fields_v alone. None of the keys here needs a JSON escape.
template <typename Type> std::string members_from_fields() {
  std::string properties{R"("properties":{)"};
  std::string required{R"("required":[)"};
  bool first_property = true;
  bool first_required = true;
  for (const FieldDescriptor& field : scrydantic::fields_v<Type>) {
    const std::string key = '"' + std::string{field.key} + '"';
    properties += (first_property ? "" : ",") + key + ':' + std::string{field.schema};
    first_property = false;
    if (field.required) {
      required += (first_required ? "" : ",") + key;
      first_required = false;
    }
  }
  return properties + "}," + required + "]";
}

// Whether the schema of `Type` holds exactly the fields that fields_v lists:
// each field's schema text under its key, in the same order, and the same
// required keys.
template <typename Type> bool schema_holds_fields(const std::string_view schema) {
  return schema.find(members_from_fields<Type>()) != std::string_view::npos;
}

// ---- fields_v ----------------------------------------------------------------

// The fields of the guide's Booking.
static_assert(fields_are<Booking>({
    {.key = "city",
     .description = "",
     .required = true,
     .schema = R"({"type":"string"})"},
    {.key = "guests",
     .description = "",
     .required = true,
     .schema = R"({"maximum":8,"minimum":1,"type":"integer"})"},
    {.key = "note",
     .description = "",
     .required = false,
     .schema = R"({"default":"","maxLength":200,"type":"string"})"},
}));

// A field is listed where keys_v lists its key, so a renamed member stands
// where its name sorts.
static_assert(scrydantic::keys_v<Row> ==
              std::array<std::string_view, 3>{"id", "title", "weight"});
static_assert(fields_are<Row>({
    {.key = "id",
     .description = "",
     .required = false,
     .schema =
         R"({"default":0,"maximum":9223372036854775807,"minimum":-9223372036854775808,"type":"integer"})"},
    {.key = "title",
     .description = "",
     .required = true,
     .schema = R"({"type":"string"})"},
    {.key = "weight",
     .description = "",
     .required = true,
     .schema = R"({"anyOf":[{"type":"number"},{"type":"null"}]})"},
}));

// `required` is what the schema says: under skip_null an optional member is
// not, emit_null makes it required again, and an initializer makes any member
// optional.
static_assert(fields_are<Patch>({
    {.key = "id",
     .description = "Row to change",
     .required = true,
     .schema = R"({"description":"Row to change","type":"string"})"},
    {.key = "priority",
     .description = "",
     .required = true,
     .schema =
         R"({"anyOf":[{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},{"type":"null"}]})"},
    {.key = "revision",
     .description = "",
     .required = false,
     .schema =
         R"({"default":1,"maximum":2147483647,"minimum":-2147483648,"type":"integer"})"},
    {.key = "title",
     .description = "",
     .required = false,
     .schema = R"({"anyOf":[{"type":"string"},{"type":"null"}]})"},
}));

// A member whose type contains its own class refers to the root of the schema
// that the fields belong to.
static_assert(fields_are<Category>({
    {.key = "children",
     .description = "Narrower categories",
     .required = true,
     .schema =
         R"({"description":"Narrower categories","items":{"$ref":"#"},"type":"array"})"},
    {.key = "name",
     .description = "",
     .required = true,
     .schema = R"({"type":"string"})"},
}));

// A member of a recursive class refers to that class's definition in the schema
// of the class that holds the member, with what the member states beside it.
static_assert(fields_are<Shelf>({
    {.key = "label",
     .description = "",
     .required = true,
     .schema = R"({"type":"string"})"},
    {.key = "root",
     .description = "Where it starts",
     .required = true,
     .schema =
         R"({"$ref":"#/$defs/field_metadata.Category","description":"Where it starts"})"},
    {.key = "spare",
     .description = "",
     .required = true,
     .schema =
         R"({"anyOf":[{"$ref":"#/$defs/field_metadata.Category"},{"type":"null"}]})"},
}));

// A flattened member's fields are listed among the object's own, and the
// member itself is not.
static_assert(fields_are<Listing>({
    {.key = "cursor",
     .description = "Opaque cursor",
     .required = true,
     .schema =
         R"({"anyOf":[{"type":"string"},{"type":"null"}],"description":"Opaque cursor"})"},
    {.key = "limit",
     .description = "",
     .required = false,
     .schema =
         R"({"default":50,"maximum":2147483647,"minimum":-2147483648,"type":"integer"})"},
    {.key = "query",
     .description = "",
     .required = true,
     .schema = R"({"type":"string"})"},
}));

// A title, custom keywords, a constraint, and a default are all in the text,
// under the key the class's rule spells.
static_assert(fields_are<Event>({
    {.key = "sky",
     .description = "",
     .required = false,
     .schema = R"({"default":"clear","enum":["overcast","clear"],"type":"string"})"},
    {.key = "startsOn",
     .description = "",
     .required = true,
     .schema = R"({"format":"date","title":"Starts","type":"string"})"},
    {.key = "tagNames",
     .description = "",
     .required = false,
     .schema =
         R"({"default":[],"items":{"type":"string"},"maxItems":3,"type":"array","uniqueItems":true})"},
}));

// The argument object of a function: a parameter's description, and a
// parameter with a default argument, which is not required and whose schema is
// that of the parameter's own type.
static_assert(fields_are<PlanArguments>({
    {.key = "city",
     .description = "City name",
     .required = true,
     .schema = R"({"description":"City name","type":"string"})"},
    {.key = "days",
     .description = "",
     .required = false,
     .schema = R"({"maximum":14,"minimum":1,"type":"integer"})"},
}));

// A type that only encoding accepts has fields and no schema.
static_assert(!scrydantic::SupportedValue<Borrowed>);
static_assert(fields_are<Borrowed>({
    {.key = "count", .description = "", .required = false, .schema = ""},
    {.key = "text", .description = "", .required = true, .schema = ""},
}));

// A tag is not a field, an empty class has none, and the type may be qualified.
static_assert(fields_are<Circle>({
    {.key = "radius",
     .description = "",
     .required = false,
     .schema = R"({"default":0,"type":"number"})"},
}));
static_assert(scrydantic::fields_v<std::monostate>.empty());
static_assert(fields_are<const Page&>({
    {.key = "cursor",
     .description = "Opaque cursor",
     .required = true,
     .schema =
         R"({"anyOf":[{"type":"string"},{"type":"null"}],"description":"Opaque cursor"})"},
    {.key = "limit",
     .description = "",
     .required = false,
     .schema =
         R"({"default":50,"maximum":2147483647,"minimum":-2147483648,"type":"integer"})"},
}));

// The list is an array of the type the header names, one field for each key.
static_assert(std::same_as<decltype(scrydantic::fields_v<Booking>),
                           const std::array<FieldDescriptor, 3>>);

template <typename Type>
constexpr bool keys_match = [] {
  const auto& fields = scrydantic::fields_v<Type>;
  const auto& keys = scrydantic::keys_v<Type>;
  if (fields.size() != keys.size()) {
    return false;
  }
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (fields[index].key != keys[index]) {
      return false;
    }
  }
  return true;
}();

static_assert(keys_match<Booking> && keys_match<Row> && keys_match<Patch> &&
              keys_match<Category> && keys_match<Shelf> && keys_match<Listing> &&
              keys_match<Event> && keys_match<Borrowed> && keys_match<PlanArguments>);

// Each field is a constant, so `template for` can visit the list as well.
template <typename Type> consteval std::size_t required_count() {
  std::size_t count = 0;
  template for (constexpr FieldDescriptor field : scrydantic::fields_v<Type>) {
    count += field.required ? 1 : 0;
  }
  return count;
}

static_assert(required_count<Booking>() == 2 && required_count<Patch>() == 2 &&
              required_count<PingArguments>() == 0);

// ---- field_members_v ---------------------------------------------------------

// The member behind each field, in the order of the keys, whatever the key is.
static_assert(scrydantic::field_members_v<Row> ==
              std::array{^^Row::identifier, ^^Row::title, ^^Row::weight});

// A flattened class's member is the one that declares the field.
static_assert(scrydantic::field_members_v<Listing> ==
              std::array{^^Page::cursor, ^^Page::limit, ^^Listing::query});

// In an argument object the member of a parameter with a default argument is an
// optional of the parameter's type, and it carries the parameter's annotations.
static_assert(std::meta::type_of(scrydantic::field_members_v<PlanArguments>[0]) ==
              std::meta::dealias(^^std::string));
static_assert(std::meta::type_of(scrydantic::field_members_v<PlanArguments>[1]) ==
              ^^std::optional<std::int32_t>);
static_assert(std::meta::annotations_of_with_type(
                  scrydantic::field_members_v<PlanArguments>[1], ^^scrydantic::range)
                  .size() == 1);

// A class without members has an empty list of each kind, which can be visited.
static_assert(scrydantic::fields_v<PingArguments>.empty());
static_assert(scrydantic::field_members_v<PingArguments>.empty());
static_assert(scrydantic::field_members_v<std::monostate>.empty());
static_assert(scrydantic::field_members_v<const Row&> ==
              scrydantic::field_members_v<Row>);

// The other library's own reading of a member: the column it is stored in, or
// -1 for a member without one.
consteval int column_of(const std::meta::info member) {
  const auto columns = std::meta::annotations_of_with_type(member, ^^column);
  return columns.empty() ? -1 : std::meta::extract<column>(columns.front()).index;
}

// One cell of a table header for each field: its key and its column, and, from
// the member's own type, whether the cell holds a number. `template for` visits
// the members at compile time, so each iteration splices its member's type.
template <typename Type> std::vector<std::string> header() {
  std::vector<std::string> cells{};
  // Unused for a class without members, whose expansion is empty.
  [[maybe_unused]] std::size_t index = 0;
  template for (constexpr std::meta::info member : scrydantic::field_members_v<Type>) {
    using Member = typename[:std::meta::type_of(member):];
    constexpr int at = column_of(member);
    cells.push_back(std::string{scrydantic::fields_v<Type>[index].key} + "@" +
                    std::to_string(at) +
                    (std::is_arithmetic_v<Member> ? " number" : " text"));
    ++index;
  }
  return cells;
}

// A spliced member reads the value behind a field of the class that declares it.
template <typename Type>
std::vector<std::string> identifiers_of_empty(const Type& row) {
  std::vector<std::string> empty{};
  template for (constexpr std::meta::info member : scrydantic::field_members_v<Type>) {
    using Member = typename[:std::meta::type_of(member):];
    if (row.[:member:] == Member{}) {
      empty.emplace_back(std::meta::identifier_of(member));
    }
  }
  return empty;
}

// ---- enumerators_v -----------------------------------------------------------

// Identifiers, in declaration order.
static_assert(names_are<Sky>({"overcast", "clear"}));
// A name annotation is the string in place of the identifier.
static_assert(names_are<Channel>({"stable", "pre-release", "nightly"}));
// A rule on the enum spells the identifiers, and a name annotation is as written.
static_assert(names_are<Mode>({"read-only", "rw", "append-only"}));
static_assert(names_are<const Sky&>({"overcast", "clear"}));
static_assert(std::same_as<decltype(scrydantic::enumerators_v<Sky>),
                           const std::array<std::string_view, 2>>);

// ---- At run time -------------------------------------------------------------

TEST_CASE("a field's schema is the text its object's schema holds under the key") {
  CHECK(schema_holds_fields<Booking>(scrydantic::schema_v<Booking>));
  CHECK(schema_holds_fields<Row>(scrydantic::schema_v<Row>));
  CHECK(schema_holds_fields<Patch>(scrydantic::schema_v<Patch>));
  CHECK(schema_holds_fields<Listing>(scrydantic::schema_v<Listing>));
  CHECK(schema_holds_fields<Event>(scrydantic::schema_v<Event>));
  CHECK(schema_holds_fields<Circle>(scrydantic::schema_v<Circle>));
  // A recursive root's references are to "#", and a holder's to its "$defs".
  CHECK(schema_holds_fields<Category>(scrydantic::schema_v<Category>));
  CHECK(schema_holds_fields<Shelf>(scrydantic::schema_v<Shelf>));
  // An argument object's schema is the function's input schema.
  CHECK(schema_holds_fields<PlanArguments>(scrydantic::input_schema_v<PlanArguments>));

  // The check tells one object's fields from another's.
  CHECK_FALSE(schema_holds_fields<Booking>(scrydantic::schema_v<Row>));
  CHECK(members_from_fields<Booking>() ==
        R"("properties":{"city":{"type":"string"},)"
        R"("guests":{"maximum":8,"minimum":1,"type":"integer"},)"
        R"("note":{"default":"","maxLength":200,"type":"string"}},)"
        R"("required":["city","guests"])");
}

TEST_CASE("fields_v lists what a value's JSON holds, in the order it is written") {
  const Booking booking{.city = "Oslo", .guests = 2, .note = "late"};
  const auto json = scrydantic::encode(booking);
  REQUIRE(json);
  const auto view = scrydantic::JsonView::parse(json->text);
  REQUIRE(view);
  const auto& fields = scrydantic::fields_v<Booking>;
  REQUIRE(view->size() == fields.size());
  for (std::size_t index = 0; index < fields.size(); ++index) {
    CHECK(view->key_at(index) == fields[index].key);
  }

  // A document that leaves out exactly the fields that are not required decodes.
  CHECK(scrydantic::decode<Booking>(R"({"city":"Oslo","guests":2})"));
  CHECK_FALSE(scrydantic::decode<Booking>(R"({"city":"Oslo","note":"late"})"));
}

// The listing in the guide's "Working with raw JSON".
TEST_CASE("the guide's listing of a Booking's fields") {
  std::ostringstream out{};
  for (const scrydantic::FieldDescriptor& field : scrydantic::fields_v<Booking>) {
    out << field.key << (field.required ? " (required) " : " ") << field.schema << '\n';
  }
  CHECK(out.str() == "city (required) {\"type\":\"string\"}\n"
                     "guests (required) {\"maximum\":8,\"minimum\":1,"
                     "\"type\":\"integer\"}\n"
                     "note {\"default\":\"\",\"maxLength\":200,\"type\":\"string\"}\n");
}

TEST_CASE("a function's parameters are listed from its argument object") {
  std::string usage{};
  for (const FieldDescriptor& field : scrydantic::fields_v<PlanArguments>) {
    usage += field.required ? " <" : " [";
    usage += field.key;
    usage += field.required ? ">" : "]";
    if (!field.description.empty()) {
      usage += " (";
      usage += field.description;
      usage += ")";
    }
  }
  CHECK(usage == " <city> (City name) [days]");

  // The flags say what a call may leave out.
  auto arguments = scrydantic::decode<PlanArguments>(R"({"city":"Oslo"})");
  REQUIRE(arguments);
  CHECK(scrydantic::apply<^^plan>(std::move(*arguments)) == "Oslo:1");
  CHECK_FALSE(scrydantic::decode<PlanArguments>(R"({"days":3})"));
}

TEST_CASE("field_members_v gives each field's member to a library's own code") {
  CHECK(header<Row>() ==
        std::vector<std::string>{"id@1 number", "title@2 text", "weight@-1 text"});
  CHECK(header<Listing>() ==
        std::vector<std::string>{"cursor@-1 text", "limit@-1 number", "query@-1 text"});
  CHECK(header<PingArguments>().empty());

  CHECK(identifiers_of_empty(Row{.title = "", .identifier = 7, .weight = 1.5}) ==
        std::vector<std::string>{"title"});
  CHECK(identifiers_of_empty(Row{.title = "a", .identifier = 0, .weight = {}}) ==
        std::vector<std::string>{"identifier", "weight"});
}

TEST_CASE("enumerators_v lists the strings that the codec reads and writes") {
  constexpr std::array modes{Mode::read_only, Mode::read_write, Mode::append_only};
  const auto& names = scrydantic::enumerators_v<Mode>;
  REQUIRE(names.size() == modes.size());
  for (std::size_t index = 0; index < names.size(); ++index) {
    const std::string text = '"' + std::string{names[index]} + '"';
    const auto encoded = scrydantic::encode(modes[index]);
    REQUIRE(encoded);
    CHECK(encoded->text == text);
    const auto decoded = scrydantic::decode<Mode>(text);
    REQUIRE(decoded);
    CHECK(*decoded == modes[index]);
  }
  CHECK(scrydantic::schema_v<Mode> ==
        R"({"enum":["read-only","rw","append-only"],"type":"string"})");
}

} // namespace field_metadata
