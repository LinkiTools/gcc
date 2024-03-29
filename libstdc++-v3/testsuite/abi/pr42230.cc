// { dg-do run }

#include <cxxabi.h>
#include <cassert>
#include <cstddef>

int main()
{
  std::size_t length = 0;
  int cc;

  char* ret = abi::__cxa_demangle("e", 0, &length, &cc);

  assert( (cc < 0 && !ret) || (ret && length) );
  std::free(ret);
  return 0;
}
