/*
 * Copyright (c) 2020, Matthew Olsson <mattco@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Function.h>
#include <LibJS/Runtime/AbstractOperations.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/PrimitiveString.h>
#include <LibJS/Runtime/RegExpConstructor.h>
#include <LibJS/Runtime/RegExpObject.h>
#include <LibJS/Runtime/StringPrototype.h>
#include <LibJS/Runtime/Value.h>
#include <LibJS/Token.h>

namespace JS {

Result<regex::RegexOptions<ECMAScriptFlags>, DeprecatedString> regex_flags_from_string(StringView flags)
{
    bool d = false, g = false, i = false, m = false, s = false, u = false, y = false, v = false;
    auto options = RegExpObject::default_flags;

    for (auto ch : flags) {
        switch (ch) {
        case 'd':
            if (d)
                return DeprecatedString::formatted(ErrorType::RegExpObjectRepeatedFlag.message(), ch);
            d = true;
            break;
        case 'g':
            if (g)
                return DeprecatedString::formatted(ErrorType::RegExpObjectRepeatedFlag.message(), ch);
            g = true;
            options |= regex::ECMAScriptFlags::Global;
            break;
        case 'i':
            if (i)
                return DeprecatedString::formatted(ErrorType::RegExpObjectRepeatedFlag.message(), ch);
            i = true;
            options |= regex::ECMAScriptFlags::Insensitive;
            break;
        case 'm':
            if (m)
                return DeprecatedString::formatted(ErrorType::RegExpObjectRepeatedFlag.message(), ch);
            m = true;
            options |= regex::ECMAScriptFlags::Multiline;
            break;
        case 's':
            if (s)
                return DeprecatedString::formatted(ErrorType::RegExpObjectRepeatedFlag.message(), ch);
            s = true;
            options |= regex::ECMAScriptFlags::SingleLine;
            break;
        case 'u':
            if (u)
                return DeprecatedString::formatted(ErrorType::RegExpObjectRepeatedFlag.message(), ch);
            u = true;
            options |= regex::ECMAScriptFlags::Unicode;
            break;
        case 'y':
            if (y)
                return DeprecatedString::formatted(ErrorType::RegExpObjectRepeatedFlag.message(), ch);
            y = true;
            // Now for the more interesting flag, 'sticky' actually unsets 'global', part of which is the default.
            options.reset_flag(regex::ECMAScriptFlags::Global);
            // "What's the difference between sticky and global, then", that's simple.
            // all the other flags imply 'global', and the "global" flag implies 'stateful';
            // however, the "sticky" flag does *not* imply 'global', only 'stateful'.
            options |= (regex::ECMAScriptFlags)regex::AllFlags::Internal_Stateful;
            options |= regex::ECMAScriptFlags::Sticky;
            break;
        case 'v':
            if (v)
                return DeprecatedString::formatted(ErrorType::RegExpObjectRepeatedFlag.message(), ch);
            v = true;
            options |= regex::ECMAScriptFlags::UnicodeSets;
            break;
        default:
            return DeprecatedString::formatted(ErrorType::RegExpObjectBadFlag.message(), ch);
        }
    }

    return options;
}

ErrorOr<DeprecatedString, ParseRegexPatternError> parse_regex_pattern(StringView pattern, bool unicode, bool unicode_sets)
{
    if (unicode && unicode_sets)
        return ParseRegexPatternError { DeprecatedString::formatted(ErrorType::RegExpObjectIncompatibleFlags.message(), 'u', 'v') };

    auto utf16_pattern_result = AK::utf8_to_utf16(pattern);
    if (utf16_pattern_result.is_error())
        return ParseRegexPatternError { "Out of memory"sv };

    auto utf16_pattern = utf16_pattern_result.release_value();
    Utf16View utf16_pattern_view { utf16_pattern };
    StringBuilder builder;

    // If the Unicode flag is set, append each code point to the pattern. Otherwise, append each
    // code unit. But unlike the spec, multi-byte code units must be escaped for LibRegex to parse.
    for (size_t i = 0; i < utf16_pattern_view.length_in_code_units();) {
        if (unicode || unicode_sets) {
            auto code_point = code_point_at(utf16_pattern_view, i);
            builder.append_code_point(code_point.code_point);
            i += code_point.code_unit_count;
            continue;
        }

        u16 code_unit = utf16_pattern_view.code_unit_at(i);
        ++i;

        if (code_unit > 0x7f)
            builder.appendff("\\u{:04x}", code_unit);
        else
            builder.append_code_point(code_unit);
    }

    return builder.to_deprecated_string();
}

ThrowCompletionOr<DeprecatedString> parse_regex_pattern(VM& vm, StringView pattern, bool unicode, bool unicode_sets)
{
    auto result = parse_regex_pattern(pattern, unicode, unicode_sets);
    if (result.is_error())
        return vm.throw_completion<JS::SyntaxError>(result.release_error().error);

    return result.release_value();
}

NonnullGCPtr<RegExpObject> RegExpObject::create(Realm& realm)
{
    return realm.heap().allocate<RegExpObject>(realm, *realm.intrinsics().regexp_prototype()).release_allocated_value_but_fixme_should_propagate_errors();
}

NonnullGCPtr<RegExpObject> RegExpObject::create(Realm& realm, Regex<ECMA262> regex, DeprecatedString pattern, DeprecatedString flags)
{
    return realm.heap().allocate<RegExpObject>(realm, move(regex), move(pattern), move(flags), *realm.intrinsics().regexp_prototype()).release_allocated_value_but_fixme_should_propagate_errors();
}

RegExpObject::RegExpObject(Object& prototype)
    : Object(ConstructWithPrototypeTag::Tag, prototype)
{
}

RegExpObject::RegExpObject(Regex<ECMA262> regex, DeprecatedString pattern, DeprecatedString flags, Object& prototype)
    : Object(ConstructWithPrototypeTag::Tag, prototype)
    , m_pattern(move(pattern))
    , m_flags(move(flags))
    , m_regex(move(regex))
{
    VERIFY(m_regex->parser_result.error == regex::Error::NoError);
}

ThrowCompletionOr<void> RegExpObject::initialize(Realm& realm)
{
    auto& vm = this->vm();
    MUST_OR_THROW_OOM(Base::initialize(realm));

    define_direct_property(vm.names.lastIndex, Value(0), Attribute::Writable);

    return {};
}

// 22.2.3.2.2 RegExpInitialize ( obj, pattern, flags ), https://tc39.es/ecma262/#sec-regexpinitialize
ThrowCompletionOr<NonnullGCPtr<RegExpObject>> RegExpObject::regexp_initialize(VM& vm, Value pattern_value, Value flags_value)
{
    // NOTE: This also contains changes adapted from https://arai-a.github.io/ecma262-compare/?pr=2418, which doesn't match the upstream spec anymore.

    // 1. If pattern is undefined, let P be the empty String.
    // 2. Else, let P be ? ToString(pattern).
    auto pattern = pattern_value.is_undefined()
        ? DeprecatedString::empty()
        : TRY(pattern_value.to_deprecated_string(vm));

    // 3. If flags is undefined, let F be the empty String.
    // 4. Else, let F be ? ToString(flags).
    auto flags = flags_value.is_undefined()
        ? DeprecatedString::empty()
        : TRY(flags_value.to_deprecated_string(vm));

    // 5. If F contains any code unit other than "d", "g", "i", "m", "s", "u", or "y" or if it contains the same code unit more than once, throw a SyntaxError exception.
    // 6. If F contains "i", let i be true; else let i be false.
    // 7. If F contains "m", let m be true; else let m be false.
    // 8. If F contains "s", let s be true; else let s be false.
    // 9. If F contains "u", let u be true; else let u be false.
    // 10. If F contains "v", let v be true; else let v be false.
    auto parsed_flags_or_error = regex_flags_from_string(flags);
    if (parsed_flags_or_error.is_error())
        return vm.throw_completion<SyntaxError>(parsed_flags_or_error.release_error());
    auto parsed_flags = parsed_flags_or_error.release_value();

    auto parsed_pattern = DeprecatedString::empty();
    if (!pattern.is_empty()) {
        bool unicode = parsed_flags.has_flag_set(regex::ECMAScriptFlags::Unicode);
        bool unicode_sets = parsed_flags.has_flag_set(regex::ECMAScriptFlags::UnicodeSets);

        // 11. If u is true, then
        //     a. Let patternText be StringToCodePoints(P).
        // 12. Else,
        //     a. Let patternText be the result of interpreting each of P's 16-bit elements as a Unicode BMP code point. UTF-16 decoding is not applied to the elements.
        // 13. Let parseResult be ParsePattern(patternText, u, v).
        parsed_pattern = TRY(parse_regex_pattern(vm, pattern, unicode, unicode_sets));
    }

    // 14. If parseResult is a non-empty List of SyntaxError objects, throw a SyntaxError exception.
    Regex<ECMA262> regex(move(parsed_pattern), parsed_flags);
    if (regex.parser_result.error != regex::Error::NoError)
        return vm.throw_completion<SyntaxError>(ErrorType::RegExpCompileError, regex.error_string());

    // 15. Assert: parseResult is a Pattern Parse Node.
    VERIFY(regex.parser_result.error == regex::Error::NoError);

    // 16. Set obj.[[OriginalSource]] to P.
    m_pattern = move(pattern);

    // 17. Set obj.[[OriginalFlags]] to F.
    m_flags = move(flags);

    // 18. Let capturingGroupsCount be CountLeftCapturingParensWithin(parseResult).
    // 19. Let rer be the RegExp Record { [[IgnoreCase]]: i, [[Multiline]]: m, [[DotAll]]: s, [[Unicode]]: u, [[CapturingGroupsCount]]: capturingGroupsCount }.
    // 20. Set obj.[[RegExpRecord]] to rer.
    // 21. Set obj.[[RegExpMatcher]] to CompilePattern of parseResult with argument rer.
    m_regex = move(regex);

    // 22. Perform ? Set(obj, "lastIndex", +0𝔽, true).
    TRY(set(vm.names.lastIndex, Value(0), Object::ShouldThrowExceptions::Yes));

    // 23. Return obj.
    return NonnullGCPtr { *this };
}

// 22.2.3.2.5 EscapeRegExpPattern ( P, F ), https://tc39.es/ecma262/#sec-escaperegexppattern
DeprecatedString RegExpObject::escape_regexp_pattern() const
{
    // 1. Let S be a String in the form of a Pattern[~UnicodeMode] (Pattern[+UnicodeMode] if F contains "u") equivalent
    //    to P interpreted as UTF-16 encoded Unicode code points (6.1.4), in which certain code points are escaped as
    //    described below. S may or may not be identical to P; however, the Abstract Closure that would result from
    //    evaluating S as a Pattern[~UnicodeMode] (Pattern[+UnicodeMode] if F contains "u") must behave identically to
    //    the Abstract Closure given by the constructed object's [[RegExpMatcher]] internal slot. Multiple calls to
    //    this abstract operation using the same values for P and F must produce identical results.
    // 2. The code points / or any LineTerminator occurring in the pattern shall be escaped in S as necessary to ensure
    //    that the string-concatenation of "/", S, "/", and F can be parsed (in an appropriate lexical context) as a
    //    RegularExpressionLiteral that behaves identically to the constructed regular expression. For example, if P is
    //    "/", then S could be "\/" or "\u002F", among other possibilities, but not "/", because /// followed by F
    //    would be parsed as a SingleLineComment rather than a RegularExpressionLiteral. If P is the empty String, this
    //    specification can be met by letting S be "(?:)".
    // 3. Return S.
    if (m_pattern.is_empty())
        return "(?:)";
    // FIXME: Check the 'u' and 'v' flags and escape accordingly
    return m_pattern.replace("\n"sv, "\\n"sv, ReplaceMode::All).replace("\r"sv, "\\r"sv, ReplaceMode::All).replace(LINE_SEPARATOR_STRING, "\\u2028"sv, ReplaceMode::All).replace(PARAGRAPH_SEPARATOR_STRING, "\\u2029"sv, ReplaceMode::All);
}

// 22.2.3.2.4 RegExpCreate ( P, F ), https://tc39.es/ecma262/#sec-regexpcreate
ThrowCompletionOr<NonnullGCPtr<RegExpObject>> regexp_create(VM& vm, Value pattern, Value flags)
{
    auto& realm = *vm.current_realm();

    // 1. Let obj be ! RegExpAlloc(%RegExp%).
    auto regexp_object = MUST(regexp_alloc(vm, *realm.intrinsics().regexp_constructor()));

    // 2. Return ? RegExpInitialize(obj, P, F).
    return TRY(regexp_object->regexp_initialize(vm, pattern, flags));
}

// 22.2.3.2 RegExpAlloc ( newTarget ), https://tc39.es/ecma262/#sec-regexpalloc
// 22.2.3.2 RegExpAlloc ( newTarget ), https://github.com/tc39/proposal-regexp-legacy-features#regexpalloc--newtarget-
ThrowCompletionOr<NonnullGCPtr<RegExpObject>> regexp_alloc(VM& vm, FunctionObject& new_target)
{
    // 1. Let obj be ? OrdinaryCreateFromConstructor(newTarget, "%RegExp.prototype%", « [[OriginalSource]], [[OriginalFlags]], [[RegExpRecord]], [[RegExpMatcher]] »).
    auto regexp_object = TRY(ordinary_create_from_constructor<RegExpObject>(vm, new_target, &Intrinsics::regexp_prototype));

    // 2. Let thisRealm be the current Realm Record.
    auto& this_realm = *vm.current_realm();

    // 3. Set the value of obj’s [[Realm]] internal slot to thisRealm.
    regexp_object->set_realm(this_realm);

    // 4. If SameValue(newTarget, thisRealm.[[Intrinsics]].[[%RegExp%]]) is true, then
    auto* regexp_constructor = this_realm.intrinsics().regexp_constructor();
    if (same_value(&new_target, regexp_constructor)) {
        // i. Set the value of obj’s [[LegacyFeaturesEnabled]] internal slot to true.
        regexp_object->set_legacy_features_enabled(true);
    }
    // 5. Else,
    else {
        // i. Set the value of obj’s [[LegacyFeaturesEnabled]] internal slot to false.
        regexp_object->set_legacy_features_enabled(false);
    }

    // 6. Perform ! DefinePropertyOrThrow(obj, "lastIndex", PropertyDescriptor { [[Writable]]: true, [[Enumerable]]: false, [[Configurable]]: false }).
    MUST(regexp_object->define_property_or_throw(vm.names.lastIndex, PropertyDescriptor { .writable = true, .enumerable = false, .configurable = false }));

    // 7. Return obj.
    return regexp_object;
}

}
