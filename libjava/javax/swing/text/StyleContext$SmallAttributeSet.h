
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_text_StyleContext$SmallAttributeSet__
#define __javax_swing_text_StyleContext$SmallAttributeSet__

#pragma interface

#include <java/lang/Object.h>
#include <gcj/array.h>

extern "Java"
{
  namespace javax
  {
    namespace swing
    {
      namespace text
      {
          class AttributeSet;
          class StyleContext;
          class StyleContext$SmallAttributeSet;
      }
    }
  }
}

class javax::swing::text::StyleContext$SmallAttributeSet : public ::java::lang::Object
{

public:
  StyleContext$SmallAttributeSet(::javax::swing::text::StyleContext *, ::javax::swing::text::AttributeSet *);
  StyleContext$SmallAttributeSet(::javax::swing::text::StyleContext *, JArray< ::java::lang::Object * > *);
  virtual ::java::lang::Object * clone();
  virtual jboolean containsAttribute(::java::lang::Object *, ::java::lang::Object *);
  virtual jboolean containsAttributes(::javax::swing::text::AttributeSet *);
  virtual ::javax::swing::text::AttributeSet * copyAttributes();
  virtual jboolean equals(::java::lang::Object *);
  virtual ::java::lang::Object * getAttribute(::java::lang::Object *);
  virtual jint getAttributeCount();
  virtual ::java::util::Enumeration * getAttributeNames();
  virtual ::javax::swing::text::AttributeSet * getResolveParent();
  virtual jint hashCode();
  virtual jboolean isDefined(::java::lang::Object *);
  virtual jboolean isEqual(::javax::swing::text::AttributeSet *);
  virtual ::java::lang::String * toString();
public: // actually package-private
  JArray< ::java::lang::Object * > * __attribute__((aligned(__alignof__( ::java::lang::Object)))) attrs;
  ::javax::swing::text::StyleContext * this$0;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_text_StyleContext$SmallAttributeSet__
