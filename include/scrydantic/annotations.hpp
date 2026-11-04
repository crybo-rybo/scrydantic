#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

/// Annotation vocabulary for Scrydantic's reflected codec and reflected tools.
///
/// Every type here is a plain C++23 structural type, so this header needs neither
/// `<meta>` nor a reflection-enabled compiler; only writing a P3394 annotation,
/// `[[= value]]`, requires C++26. Member and function annotations are written
/// before the declaration. Class annotations are written between the class-key and the
/// class name, `struct [[= scrydantic::tag{"text"}]] TextBlock { ... };`, which
/// is the placement that appertains to the class itself.
namespace scrydantic {

namespace detail {

// The payload of every text annotation: a copy of the string literal it was
// written with, so the annotation owns its text and is a structural value. Every
// member is public, as an annotation's type requires.
template <std::size_t Size> struct fixed_text {
  static_assert(Size > 0);

  /// Owned null-terminated text.
  char text[Size]{};

  /// Captures a string literal at compile time.
  /// @param value Null-terminated text.
  consteval fixed_text(const char (&value)[Size]) {
    for (std::size_t index = 0; index < Size; ++index) {
      text[index] = value[index];
    }
  }

  /// Returns the text without its null terminator.
  /// @return Non-owning view into text.
  [[nodiscard]] constexpr std::string_view view() const noexcept {
    return {text, Size - 1};
  }

protected:
  // Copies a view of Size - 1 characters; the terminator stays zero.
  consteval explicit fixed_text(const std::string_view value) {
    for (std::size_t index = 0; index < value.size(); ++index) {
      text[index] = value[index];
    }
  }
};

// The integer types a bound may be written in: those the codec reads as JSON
// integers, which leaves out bool and the character types.
template <typename Type>
concept bound_integer =
    std::integral<Type> && sizeof(Type) <= sizeof(std::uint64_t) &&
    !std::same_as<Type, bool> && !std::same_as<Type, char> &&
    !std::same_as<Type, signed char> && !std::same_as<Type, unsigned char> &&
    !std::same_as<Type, wchar_t> && !std::same_as<Type, char8_t> &&
    !std::same_as<Type, char16_t> && !std::same_as<Type, char32_t>;

template <typename Type>
concept bound_floating = std::same_as<Type, float> || std::same_as<Type, double>;

// How a bound was written.
enum class bound_form : std::uint8_t {
  none,
  integer,
  floating,
};

// One bound of a scrydantic::range or scrydantic::multiple_of, kept exactly as
// it was written: an integer as a sign and a magnitude, which together hold every
// value of every integer type the codec reads, or a floating-point value as a
// double, which holds every float. It converts implicitly from either, so a bound
// is written as a plain literal, `.min = 0` or `.max = 0.5`, and it is a
// structural type, as an annotation's members must be. Default-constructed, it is
// no bound.
struct bound {
  /// How the bound was written, or bound_form::none for no bound.
  bound_form form{};
  /// Whether an integer bound is below zero.
  bool negative{};
  /// The magnitude of an integer bound.
  std::uint64_t magnitude{};
  /// The value of a floating-point bound.
  double floating{};

  /// No bound.
  constexpr bound() noexcept = default;

  /// An integer bound.
  /// @param value Bound value.
  template <bound_integer Integer>
  // NOLINTNEXTLINE(google-explicit-constructor): a bound is written as a literal.
  consteval bound(const Integer value) noexcept : form{bound_form::integer} {
    if constexpr (std::signed_integral<Integer>) {
      negative = value < 0;
      // Through the unsigned type, so that the lowest value's magnitude does not
      // overflow.
      magnitude = negative ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                           : static_cast<std::uint64_t>(value);
    } else {
      magnitude = value;
    }
  }

  /// A floating-point bound.
  /// @param value Bound value.
  template <bound_floating Floating>
  // NOLINTNEXTLINE(google-explicit-constructor): a bound is written as a literal.
  consteval bound(const Floating value) noexcept
      : form{bound_form::floating}, floating{value} {}
};

} // namespace detail

template <std::size_t Size> struct description;

/// Builds a description annotation from a string_view naming a text catalog entry.
///
/// The view must have static storage duration, because it is bound as a reference
/// template parameter. Usage:
/// `[[= scrydantic::description_of<catalog::direction>()]]`.
/// @tparam View Annotation text to copy into the annotation payload.
/// @return Annotation equal to the one the literal form would build.
template <const std::string_view& View> [[nodiscard]] consteval auto description_of();

/// Fixed-string payload for Scrydantic's P3394 parameter-description annotation.
///
/// Applies to data members; the text becomes the member schema's `description`.
/// @tparam Size Character-array extent including the null terminator.
template <std::size_t Size> struct description : detail::fixed_text<Size> {
  using detail::fixed_text<Size>::fixed_text;

private:
  struct view_tag {};

  // The copy happens inside the type so a catalog-built annotation is still a
  // description<Size>, which is what the schema generator detects and what the
  // annotation NTTP rules require.
  consteval description(view_tag, const std::string_view value)
      : detail::fixed_text<Size>{value} {}

  template <const std::string_view& View> friend consteval auto description_of();
};

/// Deduces a description extent from a string literal.
/// @param value Annotation text whose extent is deduced.
template <std::size_t Size> description(const char (&value)[Size]) -> description<Size>;

template <const std::string_view& View> [[nodiscard]] consteval auto description_of() {
  using annotation = description<View.size() + 1>;
  return annotation{typename annotation::view_tag{}, View};
}

/// Overrides the JSON key of one data member, or the name of one tool function.
///
/// On a data member the key replaces the member's identifier in encoding,
/// decoding, generated schemas, and decode-failure paths. Objects are written in
/// lexical order of these final keys, and two members of one class that end with
/// the same key are a compile-time error. On a function annotated with
/// scrydantic::tool it replaces the function's identifier as the tool name
/// the model sees. Usage: `[[= scrydantic::name{"max_tokens"}]]`.
/// @tparam Size Character-array extent including the null terminator.
template <std::size_t Size> struct name : detail::fixed_text<Size> {
  static_assert(Size > 1, "scrydantic::name must not be empty");

  using detail::fixed_text<Size>::fixed_text;
};

/// Deduces a name extent from a string literal.
/// @param value Key text whose extent is deduced.
template <std::size_t Size> name(const char (&value)[Size]) -> name<Size>;

/// Names a class's discriminator value when it is an alternative of a `std::variant`.
///
/// A reflected `std::variant<A, B, ...>` is a tagged union: each alternative is a
/// plain aggregate with exactly one tag, the tags are distinct, and the encoded
/// object carries its alternative's tag under the key `"type"`. An alternative
/// may therefore not declare a member whose final key is `"type"`. Outside a
/// variant the tag has no effect. Usage:
/// `struct [[= scrydantic::tag{"text"}]] TextBlock { std::string text; };`.
/// @tparam Size Character-array extent including the null terminator.
template <std::size_t Size> struct tag : detail::fixed_text<Size> {
  static_assert(Size > 1, "scrydantic::tag must not be empty");

  using detail::fixed_text<Size>::fixed_text;
};

/// Deduces a tag extent from a string literal.
/// @param value Discriminator text whose extent is deduced.
template <std::size_t Size> tag(const char (&value)[Size]) -> tag<Size>;

/// Declares a function as a model-callable tool and supplies its description.
///
/// Applies to namespace-scope functions, static member functions, and member
/// functions of a toolbox class. The tool is named after the function's
/// identifier unless a scrydantic::name annotation on the same function
/// overrides it, and its argument schema is generated from the parameter list.
/// scrydantic::bind<^^function>() binds one such function,
/// scrydantic::bind<^^namespace>() every one declared in a namespace, and
/// scrydantic::bind(toolbox) every one a class declares, bound to that object.
/// Usage: `[[= scrydantic::tool{"Move the NPC one tile"}]] Position
/// step(Direction direction);`.
/// @tparam Size Character-array extent including the null terminator.
template <std::size_t Size> struct tool : detail::fixed_text<Size> {
  static_assert(Size > 1, "scrydantic::tool needs a description for the model");

  using detail::fixed_text<Size>::fixed_text;
};

/// Deduces a tool description extent from a string literal.
/// @param value Description text whose extent is deduced.
template <std::size_t Size> tool(const char (&value)[Size]) -> tool<Size>;

/// Type of the skip_null annotation.
struct skip_null_t {};

/// Omits a disengaged `std::optional` member instead of writing `null`.
///
/// On a class it applies to every `std::optional` data member; on a data member it
/// applies to that member, which must be a `std::optional`. A member it applies to
/// is also optional on input: its absence decodes as a disengaged value, whatever
/// its initializer, so every encoded value decodes to itself, and generated
/// schemas do not list it as required. Without it a disengaged member is written
/// as `null`.
inline constexpr skip_null_t skip_null{};

/// Type of the emit_null annotation.
struct emit_null_t {};

/// Restores `null` output for one `std::optional` data member of a skip_null class.
inline constexpr emit_null_t emit_null{};

/// Type of the ignore_unknown annotation.
struct ignore_unknown_t {};

/// Makes decoding of a class ignore JSON members the class does not declare.
///
/// Applies to class types. The default rejects unknown members. Generated schemas
/// stay closed (`"additionalProperties":false`), because a schema describes what a
/// producer should send, not everything the decoder tolerates.
inline constexpr ignore_unknown_t ignore_unknown{};

/// Bounds an integer or floating-point data member, as JSON Schema's `minimum`,
/// `exclusiveMinimum`, `maximum`, and `exclusiveMaximum` do.
///
/// Each field that is set becomes that keyword of the member's schema, and
/// decoding rejects a value outside it in the same words:
/// `[[= scrydantic::range{.min = 0, .max = 150}]] std::int32_t age;` has the
/// schema `{"maximum":150,"minimum":0,"type":"integer"}` and rejects 151 with
/// `$.age must be at most 150`. Where a range sets no lower or no upper bound, an
/// integer member keeps its type's, and a floating-point member has none.
///
/// A range sets at least one bound, and at most one of `min` and
/// `exclusive_min`, and of `max` and `exclusive_max`. Each bound must be exactly a
/// value of the member's type, and some value of that type must lie within the
/// range. The compiler rejects anything else: `.min = -1` cannot bound an
/// unsigned member, and `.max = 0.1` cannot bound a float, because the double 0.1
/// is not a float, so the schema and the check would bound different numbers;
/// write `0.1f`, which the schema spells as the double it is. On a std::optional
/// member the range bounds the engaged value, and through a std::indirect the
/// value it owns. Encoding does not check it, since an encoded value is the
/// program's own.
///
/// On a floating-point member, decoding checks the number twice. It checks the
/// number as the parser read it, an integer exactly and anything else as the
/// double its text rounds to, so it rejects whatever the schema rejects; then it
/// checks the value converted to the member's type, which the host gets.
/// Conversion rounds, so a float member with `.exclusive_max = 2.5f` rejects
/// `2.4999999`, which rounds to 2.5f, with
/// `must be less than 2.5 even once rounded to single precision`. JSON text more
/// precise than a double is rounded by the parser before either check, as it is
/// for any decoder that reads numbers as doubles.
struct range {
  /// Inclusive lower bound, JSON Schema's `minimum`.
  detail::bound min{};
  /// Exclusive lower bound, JSON Schema's `exclusiveMinimum`.
  detail::bound exclusive_min{};
  /// Inclusive upper bound, JSON Schema's `maximum`.
  detail::bound max{};
  /// Exclusive upper bound, JSON Schema's `exclusiveMaximum`.
  detail::bound exclusive_max{};
};

/// Requires an integer or floating-point data member to be a whole multiple of
/// a factor, as JSON Schema's `multipleOf` does.
///
/// Usage: `[[= scrydantic::multiple_of{5}]] std::int32_t step;`, whose schema
/// gains `"multipleOf":5` and whose decoding rejects 7 with
/// `$.step must be a multiple of 5`. The factor must be positive and exactly a
/// value of the member's type. For a floating-point member it must also be a
/// binary fraction whose decimal spelling is its exact value, such as 0.5 or
/// 0.125: JSON Schema tests a decimal multiple and decoding a binary one, and the
/// two agree only for such a factor. The double nearest 0.1 is not one tenth, so
/// `multiple_of{0.1}` is rejected; count tenths in an integer member instead. On
/// a floating-point member decoding checks the number twice, as parsed and as
/// converted, as scrydantic::range explains: a float member with
/// `multiple_of{0.75f}` rejects 33554433, a multiple of 0.75 that rounds to the
/// float 33554432, which is not. On a std::optional member it applies to the
/// engaged value, and through a std::indirect to the value it owns. Encoding does
/// not check it.
struct multiple_of {
  /// The factor.
  detail::bound value{};
};

/// Bounds the length of a std::string data member, counted in Unicode code
/// points as JSON Schema's `minLength` and `maxLength` count it, not in bytes:
/// `"né"` is 2 long, though it is 3 bytes of UTF-8.
///
/// Usage: `[[= scrydantic::length{.min = 1, .max = 64}]] std::string name;`,
/// whose schema gains `"maxLength":64,"minLength":1` and whose decoding rejects
/// `""` with `$.name must have at least 1 character`. A minimum of 0 and a
/// maximum of the largest std::size_t, the defaults, bound nothing and are left
/// out of the schema. A length must bound something, and its minimum may not
/// exceed its maximum. On a std::optional member it applies to the engaged value,
/// and through a std::indirect to the value it owns. Encoding does not check it.
struct length {
  /// The fewest code points, JSON Schema's `minLength`.
  std::size_t min{};
  /// The most code points, JSON Schema's `maxLength`.
  std::size_t max{std::numeric_limits<std::size_t>::max()};
};

/// Bounds the number of elements of a std::vector data member, as JSON Schema's
/// `minItems` and `maxItems` do.
///
/// Usage: `[[= scrydantic::items{.min = 1, .max = 8}]] std::vector<Tag> tags;`.
/// Its defaults and rules are those of scrydantic::length, counting elements,
/// and so are its reasons: `$.tags must have at most 8 items`. It does not apply
/// to a std::array, whose size is already fixed. On a std::optional member it
/// applies to the engaged value, and through a std::indirect to the value it
/// owns. Encoding does not check it.
struct items {
  /// The fewest elements, JSON Schema's `minItems`.
  std::size_t min{};
  /// The most elements, JSON Schema's `maxItems`.
  std::size_t max{std::numeric_limits<std::size_t>::max()};
};

/// Gives a std::string data member's schema a `pattern`, a regular expression
/// that a producer's strings must match.
///
/// **Decoding does not check it.** The pattern is a contract that the schema
/// states for the producer, not a guarantee that decoding gives the host. JSON
/// Schema's patterns are ECMA-262 regular expressions, which std::regex neither
/// matches exactly nor runs at compile time, so a check would be slow and could
/// disagree with the schema. A host that relies on the pattern checks it itself,
/// in a scrydantic::check validator on the same member. Usage:
/// `[[= scrydantic::pattern{"^[a-z]+$"}]] std::string slug;`. On a std::optional
/// member it applies to the engaged value, and through a std::indirect to the
/// value it owns.
/// @tparam Size Character-array extent including the null terminator.
template <std::size_t Size> struct pattern : detail::fixed_text<Size> {
  static_assert(Size > 1, "scrydantic::pattern must not be empty");

  using detail::fixed_text<Size>::fixed_text;
};

/// Deduces a pattern extent from a string literal.
/// @param value Regular expression whose extent is deduced.
template <std::size_t Size> pattern(const char (&value)[Size]) -> pattern<Size>;

/// Type of the check annotation, which names its validator.
/// @tparam Validator Pointer to the validator function.
template <auto Validator> struct check_t {};

/// Runs a validator on a decoded value: a member's, or a whole object's.
///
/// On a data member, `Validator` points to a function that takes the member's
/// type, or for a std::optional member its value type, as `const T&`, and returns
/// scrydantic::Status: `[[= scrydantic::check<&is_known_city>]] std::string city;`.
/// It runs once the member has decoded and met its constraint annotations, and on
/// an optional member only when the member is engaged. On a class, written
/// between the class-key and the class name, the function takes `const Class&`
/// and runs once every member of an object has decoded without an issue. Several
/// checks on one member or class run in declaration order, each only if the ones
/// before it accepted the value, so a validator may rely on those before it. The
/// compiler rejects a validator of any other signature, and one validator named
/// twice. A member that is absent and keeps its default is not checked, and
/// neither is a value being encoded, since both are the program's own.
///
/// A rejection is the Error that the validator returns, which decoding reports as
/// an IssueKind::failed_check issue at the value's path. Decoding uses its two
/// messages and nothing else, and each is one line that follows the path in a
/// sentence, as Scrydantic's own reasons do: `must be a known city`. The model is
/// told the path and the Error's `model_message`, or, when that is empty, only
/// that the value `is not valid`. The Error's `message`, the host's own text,
/// reaches the host alone; when it is empty, the host is told what the model is.
/// tool_error() builds such an Error.
///
/// A validator reports a rejection by returning it, never by throwing:
/// Scrydantic treats an exception as an allocation failure, so an exception that
/// leaves a validator leaves the decode too, and fails the call that decoded.
/// @tparam Validator Pointer to the validator function.
template <auto Validator> inline constexpr check_t<Validator> check{};

} // namespace scrydantic
