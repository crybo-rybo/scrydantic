#pragma once

namespace scrydantic {

/// What decode() accepts beyond what its strict rules do.
///
/// Every option is off by default, and a decode with all of them off is the
/// strict one. They are for a source that carries every value as text, such as
/// the variables of a URL, the fields of a form, or environment variables, and
/// for a producer that sends a nested value as JSON text inside a string.
///
/// An option changes only which JSON kind is accepted where. The value that a
/// string stands for is decoded as if it had been written in the string's place:
/// it must be within its type's range, meet its member's constraints, and pass
/// its validators, and an issue in it has the path and the reason it would have
/// had there. A string that does not hold what the type requires is reported as
/// any other value of the wrong type is, such as `must be an integer`.
///
/// Nothing else is converted. A number is not accepted for a string or for a
/// boolean, `"null"` and `""` are not `null`, and an enumerator is a string
/// already. Where the type is a `std::string` or a `Json`, a string is taken as
/// it is, whatever its text spells.
///
/// A `std::variant` with a string alternative (a `std::string` or an enum) does
/// the same: a string is that alternative, whatever its text spells. A variant
/// without one takes a string for the value it spells, under the option for
/// that value's kind, and reads the value as the alternative of its kind: with
/// coerce_scalars, `"5"` decodes into a `std::variant<int, bool>` as the `int`.
///
/// The options state what a consumer tolerates, not what a producer should send,
/// so they change no generated schema, as scrydantic::ignore_unknown changes none.
struct DecodeOptions {
  /// Accept a JSON string where a boolean or a number is required, when the
  /// string's whole text is that JSON literal: `true` or `false`, or a JSON
  /// number, with no whitespace around it. So `"5"` decodes as 5, and `" 5"`,
  /// `"+5"`, and `"0x5"` are still strings.
  bool coerce_scalars{};
  /// Accept a JSON string where an array or an object is required, when the
  /// string's text is JSON of that kind, which may have whitespace around it. So
  /// `"[1,2]"` decodes as an array of two numbers.
  ///
  /// The text counts toward max_json_depth as if it stood where its string
  /// does: the arrays and objects open around the string and the deepest
  /// nesting of the text may together be at most that many. Text that nests
  /// deeper is not accepted.
  bool coerce_structures{};
};

} // namespace scrydantic
