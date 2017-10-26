#ifndef BEERI_BASE_TYPE_TRAITS_H_
#define BEERI_BASE_TYPE_TRAITS_H_

#include <type_traits>
#include <tuple>

namespace base {

// See http://stackoverflow.com/questions/32007938/how-to-access-a-possibly-unexisting-type-alias-in-c11
// and http://stackoverflow.com/questions/27687389/how-does-void-t-work
// and
template <typename... Ts> struct voider { using type = void; };

template <typename... Ts> using void_t = typename voider<Ts...>::type;

// based on https://functionalcpp.wordpress.com/2013/08/05/function-traits/
template <class F> struct DecayedTupleFromParams;
template <class C, class R, class... Args> struct DecayedTupleFromParams<R(C::*)(Args...)> {
  typedef std::tuple<typename std::decay<Args>::type...> type;
};
template <class C, class R, class... Args> struct DecayedTupleFromParams<R(C::*)(Args...) const> {
  typedef std::tuple<typename std::decay<Args>::type...> type;
};
template <class C>
struct DecayedTupleFromParams : public DecayedTupleFromParams<decltype(&C::operator())> {};

namespace internal {

template <class F, class TUP> struct Applier;

// 6 params
template <class C, class R, class T1, class T2, class T3, class T4, class T5, class T6, class TUP>
struct Applier<R(C::*)(T1, T2, T3, T4, T5, T6), TUP> {
  using TUP0 = typename std::remove_reference<TUP>::type;
  template <class C2>
  static R call(C2&& c, TUP0&& x) {
    return c(std::move(std::get<0>(x)),
             std::move(std::get<1>(x)),
             std::move(std::get<2>(x)),
             std::move(std::get<3>(x)),
             std::move(std::get<4>(x)),
             std::move(std::get<5>(x)));
  }
};
template <class C, class R, class T1, class T2, class T3, class T4, class T5, class T6, class TUP>
struct Applier<R(C::*)(T1, T2, T3, T4, T5, T6) const, TUP> : public Applier<R(C::*)(T1, T2, T3, T4, T5, T6), TUP> {};

// 5 params
template <class C, class R, class T1, class T2, class T3, class T4, class T5, class TUP>
struct Applier<R(C::*)(T1, T2, T3, T4, T5), TUP> {
  using TUP0 = typename std::remove_reference<TUP>::type;
  template <class C2>
  static R call(C2&& c, TUP0&& x) {
    return c(std::move(std::get<0>(x)),
             std::move(std::get<1>(x)),
             std::move(std::get<2>(x)),
             std::move(std::get<3>(x)),
             std::move(std::get<4>(x)));
  }
};
template <class C, class R, class T1, class T2, class T3, class T4, class T5, class TUP>
struct Applier<R(C::*)(T1, T2, T3, T4, T5) const, TUP> : public Applier<R(C::*)(T1, T2, T3, T4, T5), TUP> {};

// 4 params
template <class C, class R, class T1, class T2, class T3, class T4, class TUP>
struct Applier<R(C::*)(T1, T2, T3, T4), TUP> {
  using TUP0 = typename std::remove_reference<TUP>::type;
  template <class C2>
  static R call(C2&& c, TUP0&& x) {
    return c(std::move(std::get<0>(x)),
             std::move(std::get<1>(x)),
             std::move(std::get<2>(x)),
             std::move(std::get<3>(x)));
  }
};
template <class C, class R, class T1, class T2, class T3, class T4, class TUP>
struct Applier<R(C::*)(T1, T2, T3, T4) const, TUP> : public Applier<R(C::*)(T1, T2, T3, T4), TUP> {};

// 3 params
template <class C, class R, class T1, class T2, class T3, class TUP>
struct Applier<R(C::*)(T1, T2, T3), TUP> {
  using TUP0 = typename std::remove_reference<TUP>::type;
  template <class C2>
  static R call(C2&& c, TUP0&& x) {
    return c(std::move(std::get<0>(x)),
             std::move(std::get<1>(x)),
             std::move(std::get<2>(x)));
  }
};
template <class C, class R, class T1, class T2, class T3, class TUP>
struct Applier<R(C::*)(T1, T2, T3) const, TUP> : public Applier<R(C::*)(T1, T2, T3), TUP> {};

// 2 params
template <class C, class R, class T1, class T2, class TUP>
struct Applier<R(C::*)(T1, T2), TUP> {
  using TUP0 = typename std::remove_reference<TUP>::type;
  template <class C2>
  static R call(C2&& c, TUP0&& x) {
    return c(std::move(std::get<0>(x)),
             std::move(std::get<1>(x)));
  }
};
template <class C, class R, class T1, class T2, class TUP>
struct Applier<R(C::*)(T1, T2) const, TUP> : public Applier<R(C::*)(T1, T2), TUP> {};

// 1 param
template <class C, class R, class T1, class TUP>
struct Applier<R(C::*)(T1), TUP> {
  using TUP0 = typename std::remove_reference<TUP>::type;
  template <class C2>
  static R call(C2&& c, TUP0&& x) {
    return c(std::move(std::get<0>(x)));
  }
};
template <class C, class R, class T1, class TUP>
struct Applier<R(C::*)(T1) const, TUP> : public Applier<R(C::*)(T1), TUP> {};

// forward object refs to their objects
template <class C, class TUP> struct Applier<C&,TUP> : public Applier<C,TUP> {};

// forward objects to their member functions
template <class C, class TUP> struct Applier : public Applier<decltype(&C::operator()), TUP> {};

} // namespace internal

template <class C, class TUP>
inline void Apply(C &&c, TUP &&tup) {
  static_assert(!std::is_lvalue_reference<TUP>::value, "Currently Apply only supported for rvalue ref TUP");
  internal::Applier<C, TUP>::call(std::forward<C>(c), std::forward<TUP>(tup));
}

} // namespace base

// Right now these macros are no-ops, and mostly just document the fact
// these types are PODs, for human use.  They may be made more contentful
// later.  The typedef is just to make it legal to put a semicolon after
// these macros.
// #define DECLARE_POD(TypeName) typedef int Dummy_Type_For_DECLARE_POD
#define PROPAGATE_POD_FROM_TEMPLATE_ARGUMENT(TemplateName)             \
    typedef int Dummy_Type_For_PROPAGATE_POD_FROM_TEMPLATE_ARGUMENT

#define GENERATE_TYPE_MEMBER_WITH_DEFAULT(Type, member, def_type)         \
template <typename T, typename = void> struct Type { using type = def_type; };  \
                                                                                 \
template <typename T> struct Type<T, ::base::void_t<typename T::member> > {   \
   using type = typename T::member; }


// specialized as has_member< T , void > or discarded (sfinae)
#define DEFINE_HAS_MEMBER(name, member) \
   template<typename , typename = void > struct name : std::false_type { };  \
   template<typename T> struct name<T, ::base::void_t<decltype(T::member)>> : std::true_type { }


// Use it like this:
// DEFINE_HAS_SIGNATURE(has_foo, T::foo, void (*)(void));
//
#define DEFINE_HAS_SIGNATURE(TraitsName, funcName, signature)       \
    template <typename U> class TraitsName {                        \
        template<typename T, T> struct helper;                      \
        template<typename T>                                        \
        static char check(helper<signature, &funcName>*);           \
        template<typename T> static long check(...);                \
    public:                                                         \
        static constexpr bool value = sizeof(check<U>(0)) == sizeof(char); \
        using type = std::integral_constant<bool, value>; \
    }


#define DEFINE_GET_FUNCTION_TRAIT(Name, FuncName, Signature)                              \
    template <typename T> class Name {                                                    \
        template<typename U, U> struct helper;                                            \
        template<typename U> static Signature Internal(helper<Signature, &U::FuncName>*)  \
                                { return &U::FuncName; }                                  \
        template<typename U> static Signature Internal(...) { return nullptr; }           \
      public:                                                                             \
        static Signature Get() { return Internal<T>(0); }                                 \
    }

#endif  // BEERI_BASE_TYPE_TRAITS_H_
