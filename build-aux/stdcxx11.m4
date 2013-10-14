# SYNOPSIS
#
#   CXX_CHECK_CXX11
#
# LICENSE
#
#   Copyright (c) 2013 Raphael Poss <r.poss@uva.nl>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 1

m4_define([CXX_COMPILE_CXX11_auto_range], [
  #include <list>
  std::list<int> y;
  void foo() { for (auto z : y) {}; }
  int main() { return 0; }
])

m4_define([CXX_COMPILE_CXX11_auto_decltype], [
    int a;
    decltype(a) b;
    auto x = a;
  int main() { return 0; }
])

m4_define([CXX_COMPILE_CXX11_right_angle_brackets], [
   #include <utility>
   std::pair<int, std::pair<int,int>> x;
  int main() { return 0; }
])

m4_define([CXX_COMPILE_CXX11_initializer_list], [
  #include <vector>
  #include <string>
  const std::vector<std::string> v = { "initializer", "list" };
  int main() { return 0; }
])

m4_define([CXX_COMPILE_CXX11_static_assert], [
  template <typename T>
    struct check
    {
      static_assert(sizeof(int) <= sizeof(T), "not big enough");
    };
    typedef check<int> check_type;
    check_type c;
  int main() { return 0; }
])

m4_define([CXX_COMPILE_CXX11_constructor_reuse], [
  template <typename T>
    struct check
    {
      check(int x) {}
      check() : check(42) {}
    };
    typedef check<int> check_type;
    check_type c;
  int main() { return 0; }
])

m4_define([CXX_COMPILE_CXX11_move_ref], [
  template <typename T>
    struct check
    { };
    typedef check<int> check_type;
    check_type c;
    check_type&& cr = static_cast<check_type&&>(c);
  int main() { return 0; }
])

AC_DEFUN([_CHECK_CXX11_FEATURE], [dnl
      cachevar=AS_TR_SH([cxx11_cv_$1_$2])
      AC_CACHE_CHECK([whether $CXX $2 supports $3], $cachevar,
                     [ac_save_CXXFLAGS="$CXXFLAGS"
                      CXXFLAGS="$CXXFLAGS $2"
                      AC_LINK_IFELSE([AC_LANG_SOURCE([CXX_COMPILE_CXX11_$1])], [eval $cachevar=yes], [eval $cachevar=no])
                      CXXFLAGS=$ac_save_CXXFLAGS])
      if eval test x\$$cachevar = xno; then ac_success=no; continue; fi
])

AC_DEFUN([CHECK_CXX11], [dnl
  AC_LANG_ASSERT([C++])dnl

  for switch in -std=gnu++11 -std=gnu++0x -std=c++11 -std=c++0x; do
      ac_success=yes

      _CHECK_CXX11_FEATURE([auto_range], [$switch], [range-based for loops])
      _CHECK_CXX11_FEATURE([auto_decltype], [$switch], [auto and decltype declarations])
      _CHECK_CXX11_FEATURE([right_angle_brackets], [$switch], [double right angle brackets in template uses (a<b<int>>)])
      _CHECK_CXX11_FEATURE([initializer_list], [$switch], [initializer lists])
      _CHECK_CXX11_FEATURE([static_assert], [$switch], [static_assert])
      _CHECK_CXX11_FEATURE([constructor_reuse], [$switch], [constructor reuse])
      _CHECK_CXX11_FEATURE([move_ref], [$switch], [move references])

      if test $ac_success = yes; then
        CXXFLAGS="$CXXFLAGS $switch"
        break
      fi
  done

  if test x$ac_success = xno; then
    AC_MSG_ERROR([*** A compiler with support for C++11 language features is required.])
  fi
])
