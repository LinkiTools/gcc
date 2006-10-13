
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_text_html_HTMLDocument__
#define __javax_swing_text_html_HTMLDocument__

#pragma interface

#include <javax/swing/text/DefaultStyledDocument.h>
#include <gcj/array.h>

extern "Java"
{
  namespace java
  {
    namespace net
    {
        class URL;
    }
  }
  namespace javax
  {
    namespace swing
    {
      namespace text
      {
          class AbstractDocument$AbstractElement;
          class AbstractDocument$AttributeContext;
          class AbstractDocument$Content;
          class AttributeSet;
          class DefaultStyledDocument$ElementSpec;
          class Element;
        namespace html
        {
            class HTML$Tag;
            class HTMLDocument;
            class HTMLDocument$Iterator;
            class HTMLEditorKit$Parser;
            class HTMLEditorKit$ParserCallback;
            class HTMLFrameHyperlinkEvent;
            class StyleSheet;
        }
      }
    }
  }
}

class javax::swing::text::html::HTMLDocument : public ::javax::swing::text::DefaultStyledDocument
{

public:
  HTMLDocument();
  HTMLDocument(::javax::swing::text::html::StyleSheet *);
  HTMLDocument(::javax::swing::text::AbstractDocument$Content *, ::javax::swing::text::html::StyleSheet *);
  virtual ::javax::swing::text::html::StyleSheet * getStyleSheet();
public: // actually protected
  virtual ::javax::swing::text::AbstractDocument$AbstractElement * createDefaultRoot();
  virtual ::javax::swing::text::Element * createLeafElement(::javax::swing::text::Element *, ::javax::swing::text::AttributeSet *, jint, jint);
  virtual ::javax::swing::text::Element * createBranchElement(::javax::swing::text::Element *, ::javax::swing::text::AttributeSet *);
public:
  virtual ::javax::swing::text::html::HTMLEditorKit$Parser * getParser();
  virtual void setParser(::javax::swing::text::html::HTMLEditorKit$Parser *);
  virtual void setTokenThreshold(jint);
  virtual jint getTokenThreshold();
  virtual ::java::net::URL * getBase();
  virtual void setBase(::java::net::URL *);
  virtual jboolean getPreservesUnknownTags();
  virtual void setPreservesUnknownTags(jboolean);
  virtual void processHTMLFrameHyperlinkEvent(::javax::swing::text::html::HTMLFrameHyperlinkEvent *);
  virtual ::javax::swing::text::html::HTMLDocument$Iterator * getIterator(::javax::swing::text::html::HTML$Tag *);
  virtual ::javax::swing::text::html::HTMLEditorKit$ParserCallback * getReader(jint);
  virtual ::javax::swing::text::html::HTMLEditorKit$ParserCallback * getReader(jint, jint, jint, ::javax::swing::text::html::HTML$Tag *);
  virtual ::javax::swing::text::html::HTMLEditorKit$ParserCallback * getInsertingReader(jint, jint, jint, ::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::Element *);
  virtual ::javax::swing::text::Element * getElement(::javax::swing::text::Element *, ::java::lang::Object *, ::java::lang::Object *);
  virtual ::javax::swing::text::Element * getElement(::java::lang::String *);
  virtual void setInnerHTML(::javax::swing::text::Element *, ::java::lang::String *);
  virtual void setOuterHTML(::javax::swing::text::Element *, ::java::lang::String *);
  virtual void insertBeforeStart(::javax::swing::text::Element *, ::java::lang::String *);
  virtual void insertBeforeEnd(::javax::swing::text::Element *, ::java::lang::String *);
  virtual void insertAfterEnd(::javax::swing::text::Element *, ::java::lang::String *);
  virtual void insertAfterStart(::javax::swing::text::Element *, ::java::lang::String *);
public: // actually package-private
  static void access$0(::javax::swing::text::html::HTMLDocument *, JArray< ::javax::swing::text::DefaultStyledDocument$ElementSpec * > *);
  static void access$1(::javax::swing::text::html::HTMLDocument *, jint, JArray< ::javax::swing::text::DefaultStyledDocument$ElementSpec * > *);
  static ::javax::swing::text::AbstractDocument$AttributeContext * access$2(::javax::swing::text::html::HTMLDocument *);
public:
  static ::java::lang::String * AdditionalComments;
public: // actually package-private
  ::java::net::URL * __attribute__((aligned(__alignof__( ::javax::swing::text::DefaultStyledDocument)))) baseURL;
  jboolean preservesUnknownTags;
  jint tokenThreshold;
  ::javax::swing::text::html::HTMLEditorKit$Parser * parser;
  ::javax::swing::text::html::StyleSheet * styleSheet;
  ::javax::swing::text::AbstractDocument$Content * content;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_text_html_HTMLDocument__
