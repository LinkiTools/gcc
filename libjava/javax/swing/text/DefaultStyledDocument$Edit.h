
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_text_DefaultStyledDocument$Edit__
#define __javax_swing_text_DefaultStyledDocument$Edit__

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
          class DefaultStyledDocument;
          class DefaultStyledDocument$Edit;
          class Element;
      }
    }
  }
}

class javax::swing::text::DefaultStyledDocument$Edit : public ::java::lang::Object
{

public:
  virtual JArray< ::javax::swing::text::Element * > * getRemovedElements();
  virtual JArray< ::javax::swing::text::Element * > * getAddedElements();
private:
  jboolean contains(::java::util::Vector *, ::javax::swing::text::Element *);
public:
  virtual void addRemovedElement(::javax::swing::text::Element *);
  virtual void addRemovedElements(JArray< ::javax::swing::text::Element * > *);
  virtual void addAddedElement(::javax::swing::text::Element *);
  virtual void addAddedElements(JArray< ::javax::swing::text::Element * > *);
  DefaultStyledDocument$Edit(::javax::swing::text::DefaultStyledDocument *, ::javax::swing::text::Element *, jint, JArray< ::javax::swing::text::Element * > *, JArray< ::javax::swing::text::Element * > *);
public: // actually package-private
  ::javax::swing::text::Element * __attribute__((aligned(__alignof__( ::java::lang::Object)))) e;
  jint index;
  ::java::util::Vector * removed;
  ::java::util::Vector * added;
  ::javax::swing::text::DefaultStyledDocument * this$0;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_text_DefaultStyledDocument$Edit__
