// Flattened members: whose fields an object has, in its schema, its encoding,
// its decoding, and keys_v; which class's annotations govern each field; where
// an issue inside one is reported; and every use of flatten that the compiler
// rejects, with its diagnostic.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <meta>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <scrydantic/tools.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace flat {

using scrydantic::check;
using scrydantic::flatten;
using scrydantic::Status;

struct Tool {
  std::string name;

  bool operator==(const Tool&) const = default;
};

struct Page {
  std::optional<std::string> next_cursor;

  bool operator==(const Page&) const = default;
};

struct ToolList {
  std::vector<Tool> tools;
  [[= flatten]] Page page;

  bool operator==(const ToolList&) const = default;
};

struct Catalog {
  std::vector<ToolList> lists;
};

// A renamed member of a flattened class keeps its key.
struct Audit {
  std::string created_at;
  [[= scrydantic::name{"updated_by"}]] std::string editor;

  bool operator==(const Audit&) const = default;
};

// Two flattened members, whose keys sort among the class's own.
struct Record {
  std::int32_t id;
  [[= flatten]] Audit audit;
  [[= flatten]] Page page;

  bool operator==(const Record&) const = default;
};

// A flattened class that flattens members of its own.
struct Listing {
  [[= flatten]] Record record;
  std::string kind;

  bool operator==(const Listing&) const = default;
};

// skip_null follows the class that declares the member.
struct[[= scrydantic::skip_null]] Cursor {
  std::optional<std::string> next_cursor;
  [[= scrydantic::emit_null]] std::optional<std::int32_t> total;
};

struct Feed {
  std::optional<std::string> title;
  [[= flatten]] Cursor cursor;
};

struct[[= scrydantic::skip_null]] Sparse {
  std::optional<std::string> note;
  [[= flatten]] Page page;
};

// A flattened class's members keep their descriptions and constraints.
struct Paging {
  [[
    = scrydantic::description{"How many to return"},
    = scrydantic::range{.min = 1, .max = 100}
  ]] std::int32_t limit = 10;
  std::optional<std::string> cursor;
};

struct Search {
  std::string query;
  [[= flatten]] Paging paging;
};

// A default member initializer on a flattened member gives each of its fields
// a value to keep.
struct Limits {
  std::int32_t limit;
  std::int32_t offset;
};

struct Query {
  std::string text;
  [[= flatten]] Limits limits{.limit = 20, .offset = 0};
};

// How many times each counting validator has run.
struct Calls {
  std::size_t ordered{};
  std::size_t fits{};
};

Calls calls{};

[[nodiscard]] Status reject(std::string model_message) {
  return std::unexpected(scrydantic::rejection(std::move(model_message)));
}

struct Window;
Status is_ordered(const Window& window);

struct[[= check<&is_ordered>]] Window {
  std::int32_t low;
  std::int32_t high;
};

Status is_ordered(const Window& window) {
  ++calls.ordered;
  return window.low <= window.high ? Status{} : reject("must have low at most high");
}

struct Booking;
Status fits(const Booking& booking);

struct[[= check<&fits>]] Booking {
  std::string room;
  [[= flatten]] Window window;
};

Status fits(const Booking& booking) {
  ++calls.fits;
  return booking.window.high - booking.window.low <= 8
             ? Status{}
             : reject("must be at most 8 hours long");
}

// Unknown members are a matter for the object being decoded, so its class
// decides them, whatever the classes it flattens say.
struct[[= scrydantic::ignore_unknown]] Lenient {
  std::int32_t limit;
};

struct Strict {
  [[= flatten]] Lenient lenient;
  std::string query;
};

struct[[= scrydantic::ignore_unknown]] Tolerant {
  [[= flatten]] Page page;
  std::string query;
};

// A variant alternative's tag sorts among its flattened fields too.
struct[[= scrydantic::tag{"audited"}]] Audited {
  [[= flatten]] Audit audit;
};

struct[[= scrydantic::tag{"paged"}]] Paged {
  [[= flatten]] Page page;
  std::string label;
};

using Outcome = std::variant<Audited, Paged>;

// A model that contains itself through a flattened member.
struct Folder;

struct Children {
  std::vector<Folder> folders;
};

struct Folder {
  std::string name;
  [[= flatten]] Children children;
};

// Encoding only reads, so it may borrow a flattened member through a reference.
struct Titled {
  std::string_view title;
  [[= flatten]] const Page& page;
};

struct Stats {
  double ratio{};
};

struct Report {
  std::string name;
  [[= flatten]] Stats stats;
};

// A class flattened where it has no fields to give adds none.
struct Nothing {};

struct Alone {
  [[= flatten]] Nothing nothing;
  [[= flatten]] Nothing more;
  std::int32_t value;
};

// What the compiler rejects.
struct Colliding {
  std::string next_cursor;
  [[= flatten]] Page page;
};

struct TwiceFlattened {
  [[= flatten]] Page first;
  [[= flatten]] Page second;
};

struct NestedColliding {
  [[= flatten]] ToolList list;
  std::int32_t tools;
};

struct OptionalFlattened {
  [[= flatten]] std::optional<Page> page;
};

struct IndirectFlattened {
  [[= flatten]] std::indirect<Page> page;
};

struct VariantFlattened {
  [[= flatten]] Outcome outcome;
};

struct ScalarFlattened {
  [[= flatten]] std::int32_t count;
};

struct NamedFlattened {
  [[ = flatten, = scrydantic::name{"paging"} ]] Page page;
};

struct DescribedFlattened {
  [[ = scrydantic::description{"Where to resume"}, = flatten ]] Page page;
};

inline Status has_cursor(const Page& page) {
  return page.next_cursor.has_value() ? Status{} : reject("must have a cursor");
}

struct CheckedFlattened {
  [[ = flatten, = check<&has_cursor> ]] Page page;
};

struct SkippedFlattened {
  [[ = flatten, = scrydantic::skip_null ]] Page page;
};

struct BoundedFlattened {
  [[ = flatten, = scrydantic::items{.max = 3} ]] Page page;
};

struct[[= flatten]] FlattenedClass {
  std::int32_t value;
};

struct Typed {
  std::string type;
};

struct[[= scrydantic::tag{"typed"}]] TypedAlternative {
  [[= flatten]] Typed typed;
};

// Every Link holds another through the member it flattens.
struct Link;

struct Next {
  std::indirect<Link> link;
};

struct Link {
  [[= flatten]] Next next;
};

// A class whose check finds Chain's fields while Chained, which they include,
// is itself still being checked: its unnamed member is yet to be reported.
struct Chain;

struct Chained {
  std::vector<Chain> chains;
  union {
    std::int32_t whole;
    float part;
  };
};

struct Chain {
  [[= flatten]] Chained chained;
};

template <typename Type>
[[nodiscard]] scrydantic::Result<Type> decode_text(const std::string_view text) {
  return scrydantic::decode<Type>(scrydantic::Json{.text = std::string{text}});
}

template <typename Type> [[nodiscard]] std::string encoded(const Type& value) {
  const auto json = scrydantic::encode(value);
  REQUIRE(json);
  return json->text;
}

// A function template because GCC 16 reports -Wshadow for each expansion of a
// `template for` in a function that is not one.
template <typename Enum> [[nodiscard]] std::string enumerator_name(const Enum value) {
  template for (constexpr std::meta::info enumerator :
                std::define_static_array(std::meta::enumerators_of(^^Enum))) {
    if (value == [:enumerator:]) {
      return std::string{std::meta::identifier_of(enumerator)};
    }
  }
  return "an undeclared enumerator";
}

// Each issue as `kind path reason`, so a failed comparison shows all three.
[[nodiscard]] std::vector<std::string> issue_lines(const scrydantic::Error& error) {
  std::vector<std::string> lines{};
  for (const auto& issue : error.issues) {
    lines.push_back(enumerator_name(issue.kind) + " " +
                    scrydantic::json_path(issue.path) + " " + issue.reason);
  }
  return lines;
}

// Copies the error rather than moving it out, as validator_tests.cpp explains.
template <typename Type>
[[nodiscard]] scrydantic::Error rejection(const std::string_view text) {
  const auto decoded = decode_text<Type>(text);
  REQUIRE_FALSE(decoded);
  return decoded.error();
}

template <typename Type>
constexpr bool keys_are(const std::vector<std::string_view>& expected) {
  const auto& keys = scrydantic::keys_v<Type>;
  return std::vector<std::string_view>{keys.begin(), keys.end()} == expected;
}

} // namespace flat

namespace search_tools {

[[= scrydantic::tool{"Search the catalog"}]] inline std::string
    search(flat::Search arguments) {
  return arguments.query + " x" + std::to_string(arguments.paging.limit) + " from " +
         arguments.paging.cursor.value_or("the start");
}

} // namespace search_tools

namespace flat {

// The flattened member has no property of its own: its fields are the object's.
static_assert(scrydantic::schema_v<ToolList> ==
              R"({"additionalProperties":false,"properties":{"next_cursor":{"anyOf":[)"
              R"({"type":"string"},{"type":"null"}]},"tools":{"items":{)"
              R"("additionalProperties":false,"properties":{"name":{"type":"string"}},)"
              R"("required":["name"],"type":"object"},"type":"array"}},)"
              R"("required":["next_cursor","tools"],"type":"object"})");

static_assert(
    scrydantic::schema_v<Record> ==
    R"({"additionalProperties":false,"properties":{"created_at":{"type":"string"},)"
    R"("id":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},)"
    R"("next_cursor":{"anyOf":[{"type":"string"},{"type":"null"}]},)"
    R"("updated_by":{"type":"string"}},)"
    R"("required":["created_at","id","next_cursor","updated_by"],"type":"object"})");

// A member's description and constraints go where its property does, and its
// initializer keeps it out of `required`.
static_assert(scrydantic::schema_v<Search> ==
              R"({"additionalProperties":false,"properties":{"cursor":{"anyOf":[)"
              R"({"type":"string"},{"type":"null"}]},"limit":{)"
              R"("description":"How many to return","maximum":100,"minimum":1,)"
              R"("type":"integer"},"query":{"type":"string"}},)"
              R"("required":["cursor","query"],"type":"object"})");

// Cursor's skip_null leaves its `next_cursor` out of `required`, and Feed, which
// has none, keeps its own optional member in.
static_assert(
    scrydantic::schema_v<Feed> ==
    R"({"additionalProperties":false,"properties":{"next_cursor":{"anyOf":[)"
    R"({"type":"string"},{"type":"null"}]},"title":{"anyOf":[{"type":"string"},)"
    R"({"type":"null"}]},"total":{"anyOf":[{"maximum":2147483647,)"
    R"("minimum":-2147483648,"type":"integer"},{"type":"null"}]}},)"
    R"("required":["title","total"],"type":"object"})");

static_assert(
    scrydantic::schema_v<Query> ==
    R"({"additionalProperties":false,"properties":{"limit":{"maximum":2147483647,)"
    R"("minimum":-2147483648,"type":"integer"},"offset":{"maximum":2147483647,)"
    R"("minimum":-2147483648,"type":"integer"},"text":{"type":"string"}},)"
    R"("required":["text"],"type":"object"})");

// The tag's property and its place in `required` sort among the flattened keys.
static_assert(
    scrydantic::schema_v<Outcome> ==
    R"({"anyOf":[{"additionalProperties":false,"properties":{)"
    R"("created_at":{"type":"string"},"type":{"enum":["audited"],"type":"string"},)"
    R"("updated_by":{"type":"string"}},"required":["created_at","type","updated_by"],)"
    R"("type":"object"},{"additionalProperties":false,"properties":{)"
    R"("label":{"type":"string"},"next_cursor":{"anyOf":[{"type":"string"},)"
    R"({"type":"null"}]},"type":{"enum":["paged"],"type":"string"}},)"
    R"("required":["label","next_cursor","type"],"type":"object"}]})");

// A root that contains itself through a flattened member refers to itself, and
// the flattened class, whose fields are written in place, is no definition.
static_assert(
    scrydantic::schema_v<Folder> ==
    R"({"additionalProperties":false,"properties":{"folders":{"items":{"$ref":"#"},)"
    R"("type":"array"},"name":{"type":"string"}},"required":["folders","name"],)"
    R"("type":"object"})");

// The flattened class is a value in its own right too, with its own schema.
static_assert(
    scrydantic::schema_v<Children> ==
    R"({"$defs":{"flat.Folder":{"additionalProperties":false,"properties":{)"
    R"("folders":{"items":{"$ref":"#/$defs/flat.Folder"},"type":"array"},)"
    R"("name":{"type":"string"}},"required":["folders","name"],"type":"object"}},)"
    R"("additionalProperties":false,"properties":{"folders":{"items":{)"
    R"("$ref":"#/$defs/flat.Folder"},"type":"array"}},"required":["folders"],)"
    R"("type":"object"})");

// keys_v lists the flattened keys where encoding writes them.
static_assert(keys_are<ToolList>({"next_cursor", "tools"}));
static_assert(keys_are<Record>({"created_at", "id", "next_cursor", "updated_by"}));
static_assert(keys_are<Listing>({"created_at", "id", "kind", "next_cursor",
                                 "updated_by"}));
static_assert(keys_are<Audited>({"created_at", "updated_by"}));
static_assert(keys_are<Alone>({"value"}));

static_assert(scrydantic::SupportedValue<Listing>);
static_assert(scrydantic::Decodable<Listing>);
static_assert(scrydantic::ToolArguments<Search>);
static_assert(scrydantic::Encodable<Titled>);
static_assert(!scrydantic::Decodable<Titled>);

// Two fields of one object may not share a key, wherever each is declared.
static_assert(scrydantic::encodable_diagnostic<Colliding>() ==
              "flat::Colliding does not satisfy scrydantic::Encodable: "
              "flat::Colliding has members next_cursor and page.next_cursor that "
              "both map to the JSON key \"next_cursor\"");
static_assert(scrydantic::encodable_diagnostic<TwiceFlattened>() ==
              "flat::TwiceFlattened does not satisfy scrydantic::Encodable: "
              "flat::TwiceFlattened has members first.next_cursor and "
              "second.next_cursor that both map to the JSON key \"next_cursor\"");
static_assert(scrydantic::decodable_diagnostic<NestedColliding>() ==
              "flat::NestedColliding does not satisfy scrydantic::Decodable: "
              "flat::NestedColliding has members list.tools and tools that both map "
              "to the JSON key \"tools\"");

// Only a member that is itself an aggregate has fields to give.
static_assert(scrydantic::supported_value_diagnostic<OptionalFlattened>() ==
              "flat::OptionalFlattened does not satisfy scrydantic::SupportedValue: "
              "flat::OptionalFlattened::page: carries scrydantic::flatten, which "
              "applies to members of aggregate type, not to "
              "std::optional<flat::Page>");
static_assert(scrydantic::supported_value_diagnostic<IndirectFlattened>() ==
              "flat::IndirectFlattened does not satisfy scrydantic::SupportedValue: "
              "flat::IndirectFlattened::page: carries scrydantic::flatten, which "
              "applies to members of aggregate type, not to "
              "std::indirect<flat::Page>");
static_assert(!scrydantic::SupportedValue<VariantFlattened>);
static_assert(scrydantic::encodable_diagnostic<ScalarFlattened>() ==
              "flat::ScalarFlattened does not satisfy scrydantic::Encodable: "
              "flat::ScalarFlattened::count: carries scrydantic::flatten, which "
              "applies to members of aggregate type, not to int");

// A flattened member has no key or value of its own to name, describe, or check.
static_assert(scrydantic::supported_value_diagnostic<NamedFlattened>() ==
              "flat::NamedFlattened does not satisfy scrydantic::SupportedValue: "
              "flat::NamedFlattened::page: carries scrydantic::flatten together with "
              "scrydantic::name, description, or check; a flattened member has no "
              "JSON key or value of its own for them to describe");
static_assert(scrydantic::encodable_diagnostic<DescribedFlattened>() ==
              "flat::DescribedFlattened does not satisfy scrydantic::Encodable: "
              "flat::DescribedFlattened::page: carries scrydantic::flatten together "
              "with scrydantic::name, description, or check; a flattened member has "
              "no JSON key or value of its own for them to describe");
static_assert(scrydantic::decodable_diagnostic<CheckedFlattened>() ==
              "flat::CheckedFlattened does not satisfy scrydantic::Decodable: "
              "flat::CheckedFlattened::page: carries scrydantic::flatten together "
              "with scrydantic::name, description, or check; a flattened member has "
              "no JSON key or value of its own for them to describe");

// What bounds a value or omits a null applies to no aggregate member, flattened
// or not.
static_assert(scrydantic::supported_value_diagnostic<SkippedFlattened>() ==
              "flat::SkippedFlattened does not satisfy scrydantic::SupportedValue: "
              "flat::SkippedFlattened::page: is not a std::optional, so skip_null and "
              "emit_null do not apply to it");
static_assert(scrydantic::supported_value_diagnostic<BoundedFlattened>() ==
              "flat::BoundedFlattened does not satisfy scrydantic::SupportedValue: "
              "flat::BoundedFlattened::page: carries scrydantic::items, which applies "
              "to std::vector members, not to flat::Page");

static_assert(scrydantic::supported_value_diagnostic<FlattenedClass>() ==
              "flat::FlattenedClass does not satisfy scrydantic::SupportedValue: "
              "flat::FlattenedClass carries scrydantic::flatten, which applies to "
              "data members");

// A flattened field may not take the key that carries a variant's tag.
static_assert(
    scrydantic::supported_value_diagnostic<std::variant<TypedAlternative>>() ==
    "std::variant<flat::TypedAlternative> does not satisfy "
    "scrydantic::SupportedValue: flat::TypedAlternative::typed.type: has "
    "the JSON key \"type\", which carries the variant's tag");
// Outside a variant the same class has no tag member to collide with.
static_assert(scrydantic::SupportedValue<TypedAlternative>);

// A flattened member is built with the object that holds it, so a model that
// holds itself through one without a std::optional or std::vector never ends.
static_assert(scrydantic::decodable_diagnostic<Link>() ==
              "flat::Link does not satisfy scrydantic::Decodable: "
              "flat::Link::next.link: flat::Link contains itself through no "
              "std::optional, std::vector, or variant alternative after the first, "
              "so default-constructing it, as decoding does, never ends");

// A recursive model's own fault is reported as such, though the check of the
// class that flattens it runs first.
static_assert(scrydantic::supported_value_diagnostic<Chained>() ==
              "flat::Chained does not satisfy scrydantic::SupportedValue: "
              "flat::Chained has an unnamed member");

TEST_CASE("a flattened member's fields are fields of the object that holds it") {
  // Designated initializers still name the member.
  const ToolList list{.tools = {{.name = "search"}}, .page = {.next_cursor = "abc"}};
  CHECK(encoded(list) == R"({"next_cursor":"abc","tools":[{"name":"search"}]})");
  CHECK(encoded(ToolList{}) == R"({"next_cursor":null,"tools":[]})");

  const auto decoded =
      decode_text<ToolList>(R"({"tools":[{"name":"search"}],"next_cursor":"abc"})");
  REQUIRE(decoded);
  CHECK(*decoded == list);

  const auto again = decode_text<ToolList>(encoded(list));
  REQUIRE(again);
  CHECK(*again == list);

  // The member itself is no key of the object.
  CHECK(
      issue_lines(rejection<ToolList>(R"({"page":{"next_cursor":null},"tools":[]})")) ==
      std::vector<std::string>{
          "unknown_member $ contains unknown member \"page\"",
          "missing_member $.next_cursor is a required member",
      });
}

TEST_CASE("several flattened members sort their keys among the object's own") {
  const Record record{.id = 7,
                      .audit = {.created_at = "monday", .editor = "ann"},
                      .page = {.next_cursor = std::nullopt}};
  const std::string_view text =
      R"({"created_at":"monday","id":7,"next_cursor":null,"updated_by":"ann"})";
  CHECK(encoded(record) == text);

  const auto decoded = decode_text<Record>(text);
  REQUIRE(decoded);
  CHECK(*decoded == record);

  const auto view = scrydantic::JsonView::parse(text);
  REQUIRE(view);
  REQUIRE(view->size() == scrydantic::keys_v<Record>.size());
  for (std::size_t index = 0; index < view->size(); ++index) {
    CHECK(view->key_at(index) == scrydantic::keys_v<Record>[index]);
  }
}

TEST_CASE("a flattened class may flatten members of its own") {
  const Listing listing{.record = {.id = 3,
                                   .audit = {.created_at = "friday", .editor = "bo"},
                                   .page = {.next_cursor = "p2"}},
                        .kind = "note"};
  const std::string_view text = R"({"created_at":"friday","id":3,"kind":"note",)"
                                R"("next_cursor":"p2","updated_by":"bo"})";
  CHECK(encoded(listing) == text);

  const auto decoded = decode_text<Listing>(text);
  REQUIRE(decoded);
  CHECK(*decoded == listing);

  // Issues are reported in declaration order, the flattened members' own
  // members where the flattened member stands.
  CHECK(
      issue_lines(rejection<Listing>(R"({"created_at":1,"kind":2,"next_cursor":3})")) ==
      std::vector<std::string>{
          "missing_member $.id is a required member",
          "wrong_type $.created_at must be a string",
          "missing_member $.updated_by is a required member",
          "wrong_type $.next_cursor must be a string",
          "wrong_type $.kind must be a string",
      });
}

TEST_CASE("skip_null follows the class that declares the member") {
  // Cursor omits its disengaged members; Feed, which holds it, does not.
  CHECK(encoded(Feed{}) == R"({"title":null,"total":null})");
  CHECK(encoded(Feed{.title = "news", .cursor = {.next_cursor = "n", .total = 2}}) ==
        R"({"next_cursor":"n","title":"news","total":2})");

  const auto feed = decode_text<Feed>(R"({"title":null,"total":null})");
  REQUIRE(feed);
  CHECK_FALSE(feed->cursor.next_cursor.has_value());
  CHECK(issue_lines(rejection<Feed>(R"({"total":null})")) ==
        std::vector<std::string>{"missing_member $.title is a required member"});

  // And the other way about: Sparse's skip_null does not reach into Page.
  CHECK(encoded(Sparse{}) == R"({"next_cursor":null})");
  CHECK(decode_text<Sparse>(R"({"next_cursor":null})"));
  CHECK(issue_lines(rejection<Sparse>("{}")) ==
        std::vector<std::string>{"missing_member $.next_cursor is a required member"});
}

TEST_CASE("a flattened member's fields keep their constraints and defaults") {
  const auto search = decode_text<Search>(R"({"cursor":null,"query":"owl"})");
  REQUIRE(search);
  CHECK(search->paging.limit == 10);

  CHECK(
      issue_lines(rejection<Search>(R"({"cursor":null,"limit":500,"query":"owl"})")) ==
      std::vector<std::string>{"violated_constraint $.limit must be at most 100"});

  // An initializer on the flattened member gives each field a value to keep.
  const auto query = decode_text<Query>(R"({"offset":5,"text":"owl"})");
  REQUIRE(query);
  CHECK(query->limits.limit == 20);
  CHECK(query->limits.offset == 5);
}

TEST_CASE("a flattened class's validators run on the flattened member") {
  calls = {};
  const auto booking = decode_text<Booking>(R"({"high":12,"low":9,"room":"a"})");
  REQUIRE(booking);
  CHECK(booking->window.low == 9);
  CHECK(calls.ordered == 1);
  CHECK(calls.fits == 1);

  // The flattened object has no location of its own, so its class's rejection
  // is reported of the object it is part of, whose own validators then wait.
  calls = {};
  CHECK(issue_lines(rejection<Booking>(R"({"high":9,"low":12,"room":"a"})")) ==
        std::vector<std::string>{"failed_check $ must have low at most high"});
  CHECK(calls.ordered == 1);
  CHECK(calls.fits == 0);

  calls = {};
  CHECK(issue_lines(rejection<Booking>(R"({"high":20,"low":9,"room":"a"})")) ==
        std::vector<std::string>{"failed_check $ must be at most 8 hours long"});
  CHECK(calls.ordered == 1);
  CHECK(calls.fits == 1);

  // A validator sees only an object whose every member is valid.
  calls = {};
  CHECK(issue_lines(rejection<Booking>(R"({"high":"noon","low":9,"room":"a"})")) ==
        std::vector<std::string>{"wrong_type $.high must be an integer"});
  CHECK(calls.ordered == 0);
  CHECK(calls.fits == 0);

  // The flattened member is whole whatever else the object holds, so its
  // validators run beside an issue that is not theirs.
  calls = {};
  CHECK(issue_lines(rejection<Booking>(R"({"high":9,"low":12,"room":7,"x":1})")) ==
        std::vector<std::string>{
            "unknown_member $ contains unknown member \"x\"",
            "wrong_type $.room must be a string",
            "failed_check $ must have low at most high",
        });
  CHECK(calls.ordered == 1);
  CHECK(calls.fits == 0);
}

TEST_CASE("an issue inside a flattened member is reported where the JSON has it") {
  CHECK(issue_lines(rejection<ToolList>(R"({"next_cursor":5,"tools":[]})")) ==
        std::vector<std::string>{"wrong_type $.next_cursor must be a string"});
  CHECK(issue_lines(rejection<ToolList>(R"({"tools":[]})")) ==
        std::vector<std::string>{"missing_member $.next_cursor is a required member"});

  const auto error = rejection<Catalog>(
      R"({"lists":[{"next_cursor":null,"tools":[]},{"next_cursor":5,"tools":[]}]})");
  CHECK(issue_lines(error) ==
        std::vector<std::string>{"wrong_type $.lists[1].next_cursor must be a string"});
  CHECK(error.issues.front().path ==
        std::vector<scrydantic::PathSegment>{"lists", std::size_t{1}, "next_cursor"});

  // Encoding names the same location.
  const auto failed = scrydantic::encode(Report{
      .name = "load", .stats = {.ratio = std::numeric_limits<double>::infinity()}});
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message == "$.ratio must be finite");
}

TEST_CASE("the class of the object being decoded decides unknown members") {
  CHECK(decode_text<Lenient>(R"({"limit":1,"extra":true})"));
  CHECK(issue_lines(rejection<Strict>(R"({"extra":true,"limit":1,"query":"q"})")) ==
        std::vector<std::string>{"unknown_member $ contains unknown member \"extra\""});

  CHECK(issue_lines(rejection<Page>(R"({"extra":true,"next_cursor":null})")) ==
        std::vector<std::string>{"unknown_member $ contains unknown member \"extra\""});
  const auto tolerant =
      decode_text<Tolerant>(R"({"extra":true,"next_cursor":"n","query":"q"})");
  REQUIRE(tolerant);
  CHECK(tolerant->page.next_cursor == "n");
}

TEST_CASE("a variant alternative's tag sorts among its flattened fields") {
  const Outcome audited{Audited{.audit = {.created_at = "monday", .editor = "ann"}}};
  const std::string_view text =
      R"({"created_at":"monday","type":"audited","updated_by":"ann"})";
  CHECK(encoded(audited) == text);

  const auto decoded = decode_text<Outcome>(text);
  REQUIRE(decoded);
  REQUIRE(std::holds_alternative<Audited>(*decoded));
  CHECK(std::get<Audited>(*decoded).audit ==
        Audit{.created_at = "monday", .editor = "ann"});

  CHECK(encoded(Outcome{Paged{.page = {.next_cursor = "n"}, .label = "more"}}) ==
        R"({"label":"more","next_cursor":"n","type":"paged"})");
  CHECK(issue_lines(rejection<Outcome>(R"({"label":"more","type":"paged"})")) ==
        std::vector<std::string>{"missing_member $.next_cursor is a required member"});
}

TEST_CASE("a model may contain itself through a flattened member") {
  const std::string_view text = R"({"folders":[{"folders":[],"name":"src"},)"
                                R"({"folders":[{"folders":[],"name":"guide"}],)"
                                R"("name":"docs"}],"name":"root"})";
  const auto folder = decode_text<Folder>(text);
  REQUIRE(folder);
  REQUIRE(folder->children.folders.size() == 2);
  CHECK(folder->children.folders[1].children.folders.front().name == "guide");
  CHECK(encoded(*folder) == text);

  CHECK(
      issue_lines(rejection<Folder>(
          R"({"folders":[{"folders":[{"name":"deep"}],"name":"src"}],"name":"root"})")) ==
      std::vector<std::string>{
          "missing_member $.folders[0].folders[0].folders is a required member"});
}

TEST_CASE("encoding reads a flattened member through a reference") {
  const Page page{.next_cursor = "n"};
  CHECK(encoded(Titled{.title = "tools", .page = page}) ==
        R"({"next_cursor":"n","title":"tools"})");
}

TEST_CASE("a flattened class with no fields adds none") {
  CHECK(encoded(Alone{.nothing = {}, .more = {}, .value = 4}) == R"({"value":4})");
  const auto alone = decode_text<Alone>(R"({"value":4})");
  REQUIRE(alone);
  CHECK(alone->value == 4);
}

TEST_CASE("a tool's argument aggregate may flatten a member") {
  auto tools = scrydantic::bind<^^search_tools>();
  REQUIRE(tools.size() == 1);
  CHECK(tools.front().input_schema.text == scrydantic::input_schema_v<Search>);
  CHECK(scrydantic::input_schema_v<Search> == scrydantic::schema_v<Search>);

  const auto found = tools.front().invoke(
      scrydantic::Json{.text = R"({"cursor":"c7","query":"owl"})"});
  REQUIRE(found);
  CHECK(found->text == R"("owl x10 from c7")");

  const auto refused = tools.front().invoke(
      scrydantic::Json{.text = R"({"cursor":null,"limit":0,"query":"owl"})"});
  REQUIRE_FALSE(refused);
  CHECK(refused.error().model_message == "$.limit must be at least 1");
}

} // namespace flat
