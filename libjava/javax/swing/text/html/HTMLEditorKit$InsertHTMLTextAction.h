
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_text_html_HTMLEditorKit$InsertHTMLTextAction__
#define __javax_swing_text_html_HTMLEditorKit$InsertHTMLTextAction__

#pragma interface

#include <javax/swing/text/html/HTMLEditorKit$HTMLTextAction.h>
extern "Java"
{
  namespace java
  {
    namespace awt
    {
      namespace event
      {
          class ActionEvent;
      }
    }
  }
  namespace javax
  {
    namespace swing
    {
        class JEditorPane;
      namespace text
      {
          class Element;
        namespace html
        {
            class HTML$Tag;
            class HTMLDocument;
            class HTMLEditorKit$InsertHTMLTextAction;
        }
      }
    }
  }
}

class javax::swing::text::html::HTMLEditorKit$InsertHTMLTextAction : public ::javax::swing::text::html::HTMLEditorKit$HTMLTextAction
{

public:
  HTMLEditorKit$InsertHTMLTextAction(::java::lang::String *, ::java::lang::String *, ::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::html::HTML$Tag *);
  HTMLEditorKit$InsertHTMLTextAction(::java::lang::String *, ::java::lang::String *, ::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::html::HTML$Tag *);
public: // actually protected
  virtual void insertHTML(::javax::swing::JEditorPane *, ::javax::swing::text::html::HTMLDocument *, jint, ::java::lang::String *, jint, jint, ::javax::swing::text::html::HTML$Tag *);
  virtual void insertAtBoundary(::javax::swing::JEditorPane *, ::javax::swing::text::html::HTMLDocument *, jint, ::javax::swing::text::Element *, ::java::lang::String *, ::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::html::HTML$Tag *);
  virtual void insertAtBoundry(::javax::swing::JEditorPane *, ::javax::swing::text::html::HTMLDocument *, jint, ::javax::swing::text::Element *, ::java::lang::String *, ::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::html::HTML$Tag *);
public:
  virtual void actionPerformed(::java::awt::event::ActionEvent *);
public: // actually protected
  ::javax::swing::text::html::HTML$Tag * __attribute__((aligned(__alignof__( ::javax::swing::text::html::HTMLEditorKit$HTMLTextAction)))) addTag;
  ::javax::swing::text::html::HTML$Tag * alternateAddTag;
  ::javax::swing::text::html::HTML$Tag * alternateParentTag;
  ::java::lang::String * html;
  ::javax::swing::text::html::HTML$Tag * parentTag;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_text_html_HTMLEditorKit$InsertHTMLTextAction__
