/* SPDX-License-Identifier: LGPL-3.0-only */
#pragma once
#include <type_traits>

#include "monomux/adt/Metaprogramming.hpp"

#define MONOMUX_MAKE_NON_COPYABLE(CLASS_NAME)                                  \
  CLASS_NAME(const CLASS_NAME&) = delete;                                      \
  CLASS_NAME& operator=(const CLASS_NAME&) = delete;
#define MONOMUX_MAKE_NON_MOVABLE(CLASS_NAME)                                   \
  CLASS_NAME(CLASS_NAME&&) = delete;                                           \
  CLASS_NAME& operator=(CLASS_NAME&&) = delete;
#define MONOMUX_MAKE_NON_COPYABLE_MOVABLE(CLASS_NAME)                          \
  MONOMUX_MAKE_NON_COPYABLE(CLASS_NAME)                                        \
  MONOMUX_MAKE_NON_MOVABLE(CLASS_NAME)

#define MONOMUX_MAKE_STRICT_TYPE(CLASS_NAME, VIRTUAL_DTOR)                     \
  CLASS_NAME() = default;                                                      \
  MONOMUX_MAKE_NON_COPYABLE_MOVABLE(CLASS_NAME)                                \
  VIRTUAL_DTOR ~CLASS_NAME() = default;

#define MONOMUX_DETAIL_FUNCTION_HEAD(                                          \
  RET_TY, NAME, ARGUMENTS, ATTRIBUTES, QUALIFIERS)                             \
  ATTRIBUTES RET_TY NAME(ARGUMENTS) QUALIFIERS

#define MONOMUX_DETAIL_FUNCTION_TEMPLATE_HEAD(                                 \
  TEMPLATES, RET_TY, NAME, ARGUMENTS, ATTRIBUTES, QUALIFIERS)                  \
  template <TEMPLATES>                                                         \
  MONOMUX_DETAIL_FUNCTION_HEAD(                                                \
    RET_TY, NAME, ARGUMENTS, ATTRIBUTES, QUALIFIERS)

#define MONOMUX_DETAIL_CONST_TYPE                                              \
  using Const = std::add_pointer_t<                                            \
    std::add_const_t<std::remove_pointer_t<decltype(this)>>>
#define MONOMUX_DETAIL_CONST_VALUE(CALL) const auto& Value = CALL
/* NOLINTBEGIN(cppcoreguidelines-pro-type-const-cast) */
#define MONOMUX_DETAIL_CONST_OBJ const_cast<Const>(this)
/* NOLINTBEGIN(bugprone-macro-parantheses) */
#define MONOMUX_DETAIL_RETURN_CAST(RET_TY, OBJ) return const_cast<RET_TY>(OBJ)
/* NOLINTEND(bugprone-macro-parantheses) */
/* NOLINTEND(cppcoreguidelines-pro-type-const-cast) */

#define MONOMUX_DETAIL_FUNCTION_BODY(RET_TY, CALL)                             \
  {                                                                            \
    MONOMUX_DETAIL_CONST_TYPE;                                                 \
    MONOMUX_DETAIL_CONST_VALUE(CALL);                                          \
    MONOMUX_DETAIL_RETURN_CAST(RET_TY, Value);                                 \
  }

#define MONOMUX_DETAIL_CALL_0(NAME) MONOMUX_DETAIL_CONST_OBJ->NAME()
#define MONOMUX_DETAIL_CALL_1(NAME, ARG_1) MONOMUX_DETAIL_CONST_OBJ->NAME(ARG_1)
#define MONOMUX_DETAIL_CALL_0_T1(NAME, TYPE_1)                                 \
  MONOMUX_DETAIL_CONST_OBJ->NAME<TYPE_1>()
#define MONOMUX_DETAIL_CALL_1_T1(NAME, TYPE_1, ARG_1)                          \
  MONOMUX_DETAIL_CONST_OBJ->NAME<TYPE_1>(ARG_1)


#define MONOMUX_MEMBER_0(RETURN_TYPE, NAME, NODISCARD, NOEXCEPT)               \
  MONOMUX_DETAIL_FUNCTION_HEAD(RETURN_TYPE, NAME, , NODISCARD, NOEXCEPT)       \
  MONOMUX_DETAIL_FUNCTION_BODY(RETURN_TYPE, MONOMUX_DETAIL_CALL_0(NAME))
#define MONOMUX_MEMBER_1(                                                      \
  RETURN_TYPE, NAME, NODISCARD, NOEXCEPT, ARG_1_TYPE, ARG_1)                   \
  MONOMUX_DETAIL_FUNCTION_HEAD(                                                \
    RETURN_TYPE, NAME, ARG_1_TYPE ARG_1, NODISCARD, NOEXCEPT)                  \
  MONOMUX_DETAIL_FUNCTION_BODY(RETURN_TYPE, MONOMUX_DETAIL_CALL_1(NAME, ARG_1))

#define MONOMUX_MEMBER_T1_0(                                                   \
  RETURN_TYPE, NAME, NODISCARD, NOEXCEPT, TYPE_1_TYPE, TYPE_1)                 \
  MONOMUX_DETAIL_FUNCTION_TEMPLATE_HEAD(                                       \
    TYPE_1_TYPE TYPE_1, RETURN_TYPE, NAME, , NODISCARD, NOEXCEPT)              \
  MONOMUX_DETAIL_FUNCTION_BODY(RETURN_TYPE,                                    \
                               MONOMUX_DETAIL_CALL_0_T1(NAME, TYPE_1))
#define MONOMUX_MEMBER_T1_1(RETURN_TYPE,                                       \
                            NAME,                                              \
                            NODISCARD,                                         \
                            NOEXCEPT,                                          \
                            TYPE_1_TYPE,                                       \
                            TYPE_1,                                            \
                            ARG_1_TYPE,                                        \
                            ARG_1)                                             \
  MONOMUX_DETAIL_FUNCTION_TEMPLATE_HEAD(TYPE_1_TYPE TYPE_1,                    \
                                        RETURN_TYPE,                           \
                                        NAME,                                  \
                                        ARG_1_TYPE ARG_1,                      \
                                        NODISCARD,                             \
                                        NOEXCEPT)                              \
  MONOMUX_DETAIL_FUNCTION_BODY(RETURN_TYPE,                                    \
                               MONOMUX_DETAIL_CALL_1_T1(NAME, TYPE_1, ARG_1))


namespace monomux
{

namespace detail
{

template <meta::index_t I, typename... Ts> struct Argument;
template <meta::index_t I, typename RetTy, typename... Args>
struct Argument<I, RetTy(Args...)>
{
  using type = typename meta::access_t<I, meta::list<Args...>>;
};
template <meta::index_t I, typename RetTy, typename... Args>
struct Argument<I, RetTy(Args...) noexcept> : Argument<I, RetTy(Args...)>
{};
template <meta::index_t I, typename RetTy, typename... Args>
struct Argument<I, RetTy (*)(Args...)> : Argument<I, RetTy(Args...)>
{};
template <meta::index_t I, typename RetTy, typename... Args>
struct Argument<I, RetTy (&)(Args...)> : Argument<I, RetTy(Args...)>
{};

template <typename> struct ReturnType;
template <typename RetTy, typename... Args> struct ReturnType<RetTy(Args...)>
{
  using type = RetTy;
};
template <typename RetTy, typename... Args>
struct ReturnType<RetTy(Args...) noexcept> : ReturnType<RetTy(Args...)>
{};
template <typename RetTy, typename... Args>
struct ReturnType<RetTy (*)(Args...)> : ReturnType<RetTy(Args...)>
{};
template <typename RetTy, typename... Args>
struct ReturnType<RetTy (&)(Args...)> : ReturnType<RetTy(Args...)>
{};

} // namespace detail

/// \returns the Nth argument type of the specified function, which must be a
/// uniquely resolved overload.
template <meta::index_t I, typename Fn>
using argument_t = typename detail::Argument<I, Fn>::type;

/// \returns the return type of the specified function, which must be a
/// uniquely resolved overload.
template <typename Fn> using return_t = typename detail::ReturnType<Fn>::type;

} // namespace monomux
