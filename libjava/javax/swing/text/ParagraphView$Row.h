
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_text_ParagraphView$Row__
#define __javax_swing_text_ParagraphView$Row__

#pragma interface

#include <javax/swing/text/BoxView.h>
extern "Java"
{
  namespace javax
  {
    namespace swing
    {
      namespace text
      {
          class Element;
          class ParagraphView;
          class ParagraphView$Row;
          class ViewFactory;
      }
    }
  }
}

class javax::swing::text::ParagraphView$Row : public ::javax::swing::text::BoxView
{

public: // actually package-private
  ParagraphView$Row(::javax::swing::text::ParagraphView *, ::javax::swing::text::Element *);
public:
  virtual jfloat getAlignment(jint);
  virtual jfloat getMaximumSpan(jint);
public: // actually protected
  virtual jint getViewIndexAtPosition(jint);
  virtual void loadChildren(::javax::swing::text::ViewFactory *);
public: // actually package-private
  ::javax::swing::text::ParagraphView * __attribute__((aligned(__alignof__( ::javax::swing::text::BoxView)))) this$0;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_text_ParagraphView$Row__
