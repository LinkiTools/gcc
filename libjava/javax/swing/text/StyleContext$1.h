
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_text_StyleContext$1__
#define __javax_swing_text_StyleContext$1__

#pragma interface

#include <java/lang/Object.h>
extern "Java"
{
  namespace javax
  {
    namespace swing
    {
      namespace text
      {
          class StyleContext$1;
          class StyleContext$SmallAttributeSet;
      }
    }
  }
}

class javax::swing::text::StyleContext$1 : public ::java::lang::Object
{

public: // actually package-private
  StyleContext$1(::javax::swing::text::StyleContext$SmallAttributeSet *);
public:
  jboolean hasMoreElements();
  ::java::lang::Object * nextElement();
public: // actually package-private
  jint __attribute__((aligned(__alignof__( ::java::lang::Object)))) i;
  ::javax::swing::text::StyleContext$SmallAttributeSet * this$1;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_text_StyleContext$1__
