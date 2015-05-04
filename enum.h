/// @file EnumInternal.h
/// Internal definitions for the enum type generator in `Enum.h`.
///
/// Several definitions must precede the public `ENUM` macro and the interface
/// defined in it. This includes helper classes and all `constexpr` functions,
/// which cannot be forward-declared. In order to make `Enum.h` more readable,
/// these definitions are placed into this file, which is included from
/// `Enum.h`.
///
/// Throughout the internal code, macro and template parameters named `EnumType`
/// stand for the class types generated by the `ENUM` macro, while parameters
/// named `EnumValue` stand for the internal C++ enum types. Roughly,
/// `EnumValue == EnumType::_Value`.
///
/// @todo Generating the values array using the `_eat_assign` template is
///     expensive, and the cost seems to be due to the instantiation of
///     compile-time objects, not due to templates. Trying statement expressions
///     (a GNU extension) didn't work, because statement expressions aren't
///     allowed "at file scope" (in this case, within a class type declared at
///     file scope).
/// @todo Compile time is currently dominated by the cost of static
///     instantiation. Try to reduce this cost by statically instantiating data
///     structures for each type, then dynamically passing them to a small
///     number of actual processing functions - which only have to be
///     instantiated once for every different underlying type. Underlying types
///     are very likely to collide.

// TODO Make it possible for enums to be map keys.
// TODO Rename internal functions to match public interface conventions.



#pragma once

#include <cstddef>          // For size_t.
#include <cstring>          // For string and memory routines.
#include <stdexcept>
#include <type_traits>

#include "enum_preprocessor_map.h"



/// Internal namespace for compile-time and private run-time functions used by
/// the enum class generator.
namespace _enum {



/// Weak symbols to allow the same data structures to be defined statically in
/// multiple translation units, then be collapsed to one definition by the
/// linker.
#define _ENUM_WEAK      __attribute__((weak))



// TODO Make these standard-compliant.
/// Template for iterable objects over enum names and values.
///
/// The iterables are intended for use with C++11 `for-each` syntax. They are
/// returned by each enum type's static `names()` and `values()` methods. For
/// example, `EnumType::values()` is an iterable over valid values of type
/// `EnumType`, and allows the following form:
///
/// ~~~{.cc}
/// for (EnumType e : EnumType::values()) {
///     // ...
/// }
/// ~~~
///
/// The iterable class is templated to reuse code between the name and value
/// iterables.
///
/// @tparam Element Type of element returned during iteration: either the enum
///     type (for iterables over `values()`) or `const char*` (for iterables
///     over `names()`).
/// @tparam EnumType The enum type.
/// @tparam ArrayType Type of the array actually being iterated over. The reason
///     this is a type parameter is because for the iterable over `values()`,
///     the underlying array type is `const EnumType::_value * const`, instead
///     of `const EnumType * const`, as one might first expect. Objects of type
///     `EnumType` are constructed on the fly during iteration from values of
///     type `EnumType::_value` (this is a no-op at run-time). For iterables
///     over `names()`, `ArrayType` is simply `const char * const`, as would be
///     expeted.
///
/// @internal
///
/// An `_Iterable` stores a reference to the array (of either names or values)
/// that will be iterated over. `_Iterable::iterator` additionally stores an
/// index into the array. The iterator begins at the first valid index. Each
/// time it is incremented, the iterator advances to the next valid index. The
/// `end()` iterator stores an index equal to the size of the array. Values are
/// considered valid if they are not equal to the bad value, are not below the
/// minimum value, and are not above the maximum value. Names are valid if they
/// are the name of a valid value.

template <typename EnumType, typename Iterator>
class _Iterable;

template <typename EnumType, typename Derived>
class _BaseIterator {
  public:
    Derived& operator ++()
        { ++_index; return static_cast<Derived&>(*this); }
    constexpr bool operator ==(const Derived &other) const
        { return other._index == _index; }
    constexpr bool operator !=(const Derived &other) const
        { return other._index != _index; }

  protected:
    constexpr _BaseIterator(size_t index) : _index(index) { }

    size_t  _index;
};

template <typename EnumType>
class _ValueIterator :
    public _BaseIterator<EnumType, _ValueIterator<EnumType>> {

    using _Super = _BaseIterator<EnumType, _ValueIterator<EnumType>>;

  public:
    constexpr EnumType operator *() const
        { return EnumType::_value_array[_Super::_index]; }

  private:
    using _Super::_Super;

    friend _Iterable<EnumType, _ValueIterator<EnumType>>;
};

template <typename EnumType>
class _NameIterator :
    public _BaseIterator<EnumType, _NameIterator<EnumType>> {

    using _Super = _BaseIterator<EnumType, _NameIterator<EnumType>>;

  public:
    const char* operator *() const
        { return EnumType::_getProcessedName(_Super::_index); }

  private:
    using _Super::_Super;

    friend _Iterable<EnumType, _NameIterator<EnumType>>;
};

template <typename EnumType, typename Iterator>
class _Iterable {
  public:
    using iterator = Iterator;

    constexpr iterator begin() const { return iterator(0); }
    constexpr iterator end() const { return iterator(EnumType::_size); }
    constexpr size_t size() const { return EnumType::size(); }

  private:
    constexpr _Iterable() { };

    friend EnumType;
};



/// Compile-time helper class used to transform expressions of the forms `A` and
/// `A = 42` into values of type `UnderlyingType` that can be used in
/// initializer lists. The `ENUM` macro is passed a mixture of simple enum
/// constants (`A`) and constants with an explicitly-assigned value (`A = 42`).
/// Both must be turned into expressions of type `UnderlyingType` in order to be
/// usable in initializer lists of the values array. This is done by prepending
/// a cast to the expression, as follows:
/// ~~~{.cc}
/// (_eat_assign<UnderlyingType>)A
/// (_eat_assign<UnderlyingType>)A = 42
/// ~~~
/// The second case is the interesting one. At compile time, the value `A` is
/// first converted to an equivalent `_eat_assign<UnderlyingType>` object, that
/// stores the value. This object has an overriden assignment operator, which
/// "eats" the `= 42` and returns the stored value of `A`, which is then used in
/// the initializer list.
/// @tparam UnderlyingType Final type used in the values array.
template <typename UnderlyingType>
class _eat_assign {
  private:
    UnderlyingType  _value;

  public:
    explicit constexpr _eat_assign(UnderlyingType value) : _value(value) { }
    template <typename Any>
    constexpr UnderlyingType operator =(Any dummy) const
        { return _value; }
    constexpr operator UnderlyingType () const { return _value; }
};

/// Prepends its second argument with the cast `(_eat_assign<UnderlyingType>)`
/// in order to make it usable in initializer lists. See `_eat_assign`.
#define _ENUM_EAT_ASSIGN_SINGLE(UnderlyingType, expression)                    \
    ((_enum::_eat_assign<UnderlyingType>)expression)

/// Prepends each of its arguments with the casts
/// `(_eat_assign<UnderlyingType>)`, creating the elements of an initializer
/// list of objects of type `UnderlyingType`.
#define _ENUM_EAT_ASSIGN(UnderlyingType, ...)                                  \
    _ENUM_PP_MAP(_ENUM_EAT_ASSIGN_SINGLE, UnderlyingType, __VA_ARGS__)



/// Stringizes its second argument. The first argument is not used - it is there
/// only because `_ENUM_PP_MAP` expects it.
#define _ENUM_STRINGIZE_SINGLE(ignored, expression)     #expression

/// Stringizes each of its arguments.
#define _ENUM_STRINGIZE(...)                                                   \
    _ENUM_PP_MAP(_ENUM_STRINGIZE_SINGLE, ignored, __VA_ARGS__)



/// Symbols that end a constant name. Constant can be defined in several ways,
/// for example:
/// ~~~{.cc}
/// A
/// A = AnotherConstant
/// A = 42
/// A=42
/// ~~~
/// These definitions are stringized in their entirety by `_ENUM_STRINGIZE`.
/// This means that in addition to the actual constant names, the raw `_names`
/// arrays potentially contain additional trailing symbols. `_ENUM_NAME_ENDERS`
/// defines an array of symbols that would end the part of the string that is
/// the actual constant name. Note that it is important that the null terminator
/// is implicitly present in this array.
#define _ENUM_NAME_ENDERS   "= \t\n"

/// Compile-time function that determines whether a character terminates the
/// name portion of an enum constant definition.
///
/// Call as `_endsName(c)`.
///
/// @param c Character to be tested.
/// @param index Current index into the `_ENUM_NAME_ENDERS` array.
/// @return `true` if and only if `c` is one of the characters in
///     `_ENUM_NAME_ENDERS`, including the implicit null terminator in that
///     array.
constexpr bool _endsName(char c, size_t index = 0)
{
    return
        // First, test whether c is equal to the current character in
        // _ENUM_NAME_ENDERS. In the case where c is the null terminator, this
        // will cause _endsName to return true when it has exhausted
        // _ENUM_NAME_ENDERS.
        c == _ENUM_NAME_ENDERS[index]    ? true  :
        // If _ENUM_NAME_ENDERS has been exhausted and c never matched, return
        // false.
        _ENUM_NAME_ENDERS[index] == '\0' ? false :
        // Otherwise, go on to the next character in _ENUM_ENDERS.
        _endsName(c, index + 1);
}

constexpr char _toLowercaseAscii(char c)
{
    return c >= 0x41 && c <= 0x5A ? c + 0x20 : c;
}

/// Compile-time function that matches a stringized name (with potential
/// trailing spaces and equals signs) against a reference name (a regular
/// null-terminated string).
///
/// Call as `_namesMatch(stringizedName, referenceName)`.
///
/// @param stringizedName A stringized constant name, potentially terminated by
///     one of the symbols in `_ENUM_NAME_ENDERS` instead of a null terminator.
/// @param referenceName A name of interest. Null-terminated.
/// @param index Current index into both names.
/// @return `true` if and only if the portion of `stringizedName` before any of
///     the symbols in `_ENUM_NAME_ENDERS` exactly matches `referenceName`.
constexpr bool _namesMatch(const char *stringizedName,
                           const char *referenceName,
                           size_t index = 0)
{
    return
        // If the current character in the stringized name is a name ender,
        // return true if the reference name ends as well, and false otherwise.
        _endsName(stringizedName[index]) ? referenceName[index] == '\0' :
        // The current character in the stringized name is not a name ender. If
        // the reference name ended, then it is too short, so return false.
        referenceName[index] == '\0'     ? false                        :
        // Neither name has ended. If the two current characters don't match,
        // return false.
        stringizedName[index] !=
            referenceName[index]         ? false                        :
        // Otherwise, if the characters match, continue by comparing the rest of
        // the names.
        _namesMatch(stringizedName, referenceName, index + 1);
}

constexpr bool _namesMatchNocase(const char *stringizedName,
                                 const char *referenceName,
                                 size_t index = 0)
{
    return
        _endsName(stringizedName[index]) ? referenceName[index] == '\0' :
        referenceName[index] == '\0' ? false :
        _toLowercaseAscii(stringizedName[index]) !=
            _toLowercaseAscii(referenceName[index]) ? false :
        _namesMatchNocase(stringizedName, referenceName, index + 1);
}

#define _ENUM_NOT_FOUND     ((size_t)-1)



/// Functions and types used to compute range properties such as the minimum and
/// maximum declared enum values, and the total number of valid enum values.
namespace _range {

template <typename UnderlyingType>
constexpr UnderlyingType _findMinLoop(const UnderlyingType *values,
                                      size_t valueCount, size_t index,
                                      UnderlyingType best)
{
    return
        index == valueCount ? best :
        values[index] < best ?
            _findMinLoop(values, valueCount, index + 1, values[index]) :
            _findMinLoop(values, valueCount, index + 1, best);
}

template <typename UnderlyingType>
constexpr UnderlyingType _findMin(const UnderlyingType *values,
                                  size_t valueCount)
{
    return _findMinLoop(values, valueCount, 1, values[0]);
}

template <typename UnderlyingType>
constexpr UnderlyingType _findMaxLoop(const UnderlyingType *values,
                                      size_t valueCount, size_t index,
                                      UnderlyingType best)
{
    return
        index == valueCount ? best :
        values[index] > best ?
            _findMaxLoop(values, valueCount, index + 1, values[index]) :
            _findMaxLoop(values, valueCount, index + 1, best);
}

template <typename UnderlyingType>
constexpr UnderlyingType _findMax(const UnderlyingType *values, size_t count)
{
    return _findMaxLoop(values, count, 1, values[0]);
}

} // namespace _range

} // namespace _enum

// TODO Note that the static_assert for _rawSize > 0 never really gets a chance
// to fail in practice, because the preprocessor macros break before that.



namespace _enum {

// TODO Consider reserving memory statically. This will probably entail a great
// compile-time slowdown, however.
static inline const char * const* _processNames(const char * const *rawNames,
                                                size_t count)
{
    // Allocate the replacement names array.
    const char      **processedNames = new const char*[count];
    if (processedNames == nullptr)
        return nullptr;

    // Count the number of bytes needed in the replacement names array (an upper
    // bound).
    size_t          bytesNeeded = 0;
    for (size_t index = 0; index < count; ++index)
        bytesNeeded += std::strlen(rawNames[index]) + 1;

    // Allocate memory for the string data.
    char            *nameStorage = new char[bytesNeeded];
    if (nameStorage == nullptr) {
        delete[] processedNames;
        return nullptr;
    }

    // Trim each name and place the result in storage, then save a pointer to
    // it.
    char            *writePointer = nameStorage;
    for (size_t index = 0; index < count; ++index) {
        const char  *nameEnd =
            std::strpbrk(rawNames[index], _ENUM_NAME_ENDERS);

        size_t      symbolCount =
            nameEnd == nullptr ?
                std::strlen(rawNames[index]) :
                nameEnd - rawNames[index];

        std::strncpy(writePointer, rawNames[index], symbolCount);
        processedNames[index] = writePointer;
        writePointer += symbolCount;

        *writePointer = '\0';
        ++writePointer;
    }

    return processedNames;
}

#define _ENUM_TAG(EnumType)     _tag_ ## EnumType
#define _ENUM_TAG_DECLARATION(EnumType)                                        \
    namespace _enum {                                                          \
        struct _ENUM_TAG(EnumType);                                            \
    }

template <typename Tag> class _GeneratedArrays;

#define _ENUM_ARRAYS(EnumType, UnderlyingType, Tag, ...)                       \
    namespace _enum {                                                          \
                                                                               \
    template <>                                                                \
    class _GeneratedArrays<Tag> {                                              \
      protected:                                                               \
        using _Integral = UnderlyingType;                                      \
                                                                               \
      public:                                                                  \
        constexpr static const char* _name = #EnumType;                        \
                                                                               \
        enum _Enumerated : _Integral { __VA_ARGS__ };                          \
                                                                               \
      protected:                                                               \
        constexpr static _Enumerated        _value_array[] =                   \
            { _ENUM_EAT_ASSIGN(_Enumerated, __VA_ARGS__) };                    \
                                                                               \
        constexpr static const char         *_name_array[] =                   \
            { _ENUM_STRINGIZE(__VA_ARGS__) };                                  \
    };                                                                         \
                                                                               \
    }

template <typename Tag>
class _Enum : public _GeneratedArrays<Tag> {
  protected:
    using _arrays = _GeneratedArrays<Tag>;
    using _arrays::_value_array;
    using _arrays::_name_array;

  public:
    using typename _arrays::_Enumerated;
    using typename _arrays::_Integral;

    constexpr static const size_t       _size =
        sizeof(_value_array) / sizeof(_Enumerated);
    static_assert(_size > 0, "no constants defined in enum type");

    constexpr static const _Enumerated  _first = _value_array[0];
    constexpr static const _Enumerated  _last = _value_array[_size - 1];
    constexpr static const _Enumerated  _min =
        _range::_findMin(_value_array, _size);
    constexpr static const _Enumerated  _max =
        _range::_findMax(_value_array, _size);

    constexpr static const size_t       _span = _max - _min + 1;

    _Enum() = delete;
    constexpr _Enum(_Enumerated constant) : _value(constant) { }

    constexpr _Integral to_int() const
    {
        return _value;
    }

    constexpr static const _Enum _from_int(_Integral value)
    {
        return _value_array[_from_int_loop(value, true)];
    }

    constexpr static const _Enum _from_int_unchecked(_Integral value)
    {
        return (_Enumerated)value;
    }

    const char* to_string() const
    {
        _processNames();

        for (size_t index = 0; index < _size; ++index) {
            if (_value_array[index] == _value)
                return _processedNames[index];
        }

        throw std::domain_error("Enum::_to_string: invalid enum value");
    }

    constexpr static const _Enum _from_string(const char *name)
    {
        return _value_array[_from_string_loop(name, true)];
    }

    constexpr static const _Enum _from_string_nocase(const char *name)
    {
        return _value_array[_from_string_nocase_loop(name, true)];
    }

    constexpr static bool _is_valid(_Integral value)
    {
        return _from_int_loop(value, false) != _ENUM_NOT_FOUND;
    }

    constexpr static bool _is_valid(const char *name)
    {
        return _from_string_loop(name, false) != _ENUM_NOT_FOUND;
    }

    constexpr static bool _is_valid_nocase(const char *name)
    {
        return _from_string_nocase_loop(name, false) != _ENUM_NOT_FOUND;
    }

    constexpr operator _Enumerated() const { return _value; }

  protected:
    _Enumerated                         _value;

    static const char * const           *_processedNames;

    static void _processNames()
    {
        if (_processedNames == nullptr)
            _processedNames = _enum::_processNames(_name_array, _size);
    }

    static const char* _getProcessedName(size_t index)
    {
        _processNames();
        return _processedNames[index];
    }

    using _ValueIterable = _Iterable<_Enum, _ValueIterator<_Enum>>;
    using _NameIterable  = _Iterable<_Enum, _NameIterator<_Enum>>;

    friend _ValueIterator<_Enum>;
    friend _NameIterator<_Enum>;

  public:
    static const _ValueIterable     _values;
    static const _NameIterable      _names;

  protected:
    constexpr static size_t _from_int_loop(_Integral value,
                                           bool throw_exception,
                                           size_t index = 0)
    {
        return
            index == _size ?
                (throw_exception ?
                    throw std::runtime_error(
                        "Enum::from_int: invalid integer value") :
                    _ENUM_NOT_FOUND) :
            _value_array[index] == value ? index :
            _from_int_loop(value, throw_exception, index + 1);
    }

    constexpr static size_t _from_string_loop(const char *name,
                                              bool throw_exception,
                                              size_t index = 0)
    {
        return
            index == _size ?
                (throw_exception ?
                    throw std::runtime_error(
                        "Enum::_from_string: invalid string argument") :
                    _ENUM_NOT_FOUND) :
            _namesMatch(_name_array[index], name) ? index :
            _from_string_loop(name, throw_exception, index + 1);
    }

    constexpr static size_t _from_string_nocase_loop(const char *name,
                                                     bool throw_exception,
                                                     size_t index = 0)
    {
        return
            index == _size ?
                (throw_exception ?
                    throw std::runtime_error(
                        "Enum::_from_string_nocase: invalid string argument") :
                    _ENUM_NOT_FOUND) :
                _namesMatchNocase(_name_array[index], name) ? index :
                _from_string_nocase_loop(name, throw_exception, index + 1);
    }

  public:
    constexpr bool operator ==(const _Enum &other) const
        { return _value == other._value; }
    constexpr bool operator ==(const _Enumerated value) const
        { return _value == value; }
    template <typename T> bool operator ==(T other) const = delete;

    constexpr bool operator !=(const _Enum &other) const
        { return !(*this == other); }
    constexpr bool operator !=(const _Enumerated value) const
        { return !(*this == value); }
    template <typename T> bool operator !=(T other) const = delete;

    constexpr bool operator <(const _Enum &other) const
        { return _value < other._value; }
    constexpr bool operator <(const _Enumerated value) const
        { return _value < value; }
    template <typename T> bool operator <(T other) const = delete;

    constexpr bool operator <=(const _Enum &other) const
        { return _value <= other._value; }
    constexpr bool operator <=(const _Enumerated value) const
        { return _value <= value; }
    template <typename T> bool operator <=(T other) const = delete;

    constexpr bool operator >(const _Enum &other) const
        { return _value > other._value; }
    constexpr bool operator >(const _Enumerated value) const
        { return _value > value; }
    template <typename T> bool operator >(T other) const = delete;

    constexpr bool operator >=(const _Enum &other) const
        { return _value >= other._value; }
    constexpr bool operator >=(const _Enumerated value) const
        { return _value >= value; }
    template <typename T> bool operator >=(T other) const = delete;

    int operator -() const = delete;
    template <typename T> int operator +(T other) const = delete;
    template <typename T> int operator -(T other) const = delete;
    template <typename T> int operator *(T other) const = delete;
    template <typename T> int operator /(T other) const = delete;
    template <typename T> int operator %(T other) const = delete;

    template <typename T> int operator <<(T other) const = delete;
    template <typename T> int operator >>(T other) const = delete;

    int operator ~() const = delete;
    template <typename T> int operator &(T other) const = delete;
    template <typename T> int operator |(T other) const = delete;
    template <typename T> int operator ^(T other) const = delete;

    int operator !() const = delete;
    template <typename T> int operator &&(T other) const = delete;
    template <typename T> int operator ||(T other) const = delete;
};

#define _ENUM_GLOBALS(EnumType, Tag)                                           \
    namespace _enum {                                                          \
                                                                               \
    constexpr const EnumType operator +(EnumType::_Enumerated enumerated)      \
        { return (EnumType)enumerated; }                                       \
                                                                               \
    template <>                                                                \
    constexpr EnumType::_ValueIterable _ENUM_WEAK EnumType::_values{};         \
                                                                               \
    template <>                                                                \
    constexpr EnumType::_NameIterable _ENUM_WEAK EnumType::_names{};           \
                                                                               \
    constexpr _GeneratedArrays<Tag>::_Enumerated _ENUM_WEAK                    \
        _GeneratedArrays<Tag>::_value_array[];                                 \
                                                                               \
    constexpr const char * _ENUM_WEAK _GeneratedArrays<Tag>::_name_array[];    \
                                                                               \
    template <>                                                                \
    const char * const * _ENUM_WEAK EnumType::_processedNames = nullptr;       \
                                                                               \
    }

}

#define ENUM(EnumType, UnderlyingType, ...)                                    \
    _ENUM_TAG_DECLARATION(EnumType);                                           \
    _ENUM_ARRAYS(EnumType, UnderlyingType, _ENUM_TAG(EnumType), __VA_ARGS__);  \
    using EnumType = _enum::_Enum<_enum::_ENUM_TAG(EnumType)>;                 \
    _ENUM_GLOBALS(EnumType, _ENUM_TAG(EnumType));
