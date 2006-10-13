
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_text_html_HTMLDocument$HTMLReader__
#define __javax_swing_text_html_HTMLDocument$HTMLReader__

#pragma interface

#include <javax/swing/text/html/HTMLEditorKit$ParserCallback.h>
#include <gcj/array.h>

extern "Java"
{
  namespace javax
  {
    namespace swing
    {
      namespace text
      {
          class MutableAttributeSet;
        namespace html
        {
            class HTML$Tag;
            class HTMLDocument;
            class HTMLDocument$HTMLReader;
            class HTMLDocument$HTMLReader$TagAction;
        }
      }
    }
  }
}

class javax::swing::text::html::HTMLDocument$HTMLReader : public ::javax::swing::text::html::HTMLEditorKit$ParserCallback
{

public: // actually package-private
  virtual void print(::java::lang::String *);
public:
  HTMLDocument$HTMLReader(::javax::swing::text::html::HTMLDocument *, jint);
  HTMLDocument$HTMLReader(::javax::swing::text::html::HTMLDocument *, jint, jint, jint, ::javax::swing::text::html::HTML$Tag *);
public: // actually package-private
  virtual void initTags();
public: // actually protected
  virtual void pushCharacterStyle();
  virtual void popCharacterStyle();
  virtual void registerTag(::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::html::HTMLDocument$HTMLReader$TagAction *);
public:
  virtual void flush();
  virtual void handleText(JArray< jchar > *, jint);
private:
  jboolean shouldInsert();
public:
  virtual void handleStartTag(::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::MutableAttributeSet *, jint);
  virtual void handleComment(JArray< jchar > *, jint);
  virtual void handleEndTag(::javax::swing::text::html::HTML$Tag *, jint);
  virtual void handleSimpleTag(::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::MutableAttributeSet *, jint);
  virtual void handleEndOfLineString(::java::lang::String *);
public: // actually protected
  virtual void textAreaContent(JArray< jchar > *);
  virtual void preContent(JArray< jchar > *);
  virtual void blockOpen(::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::MutableAttributeSet *);
  virtual void blockClose(::javax::swing::text::html::HTML$Tag *);
  virtual void addContent(JArray< jchar > *, jint, jint);
  virtual void addContent(JArray< jchar > *, jint, jint, jboolean);
  virtual void addSpecialElement(::javax::swing::text::html::HTML$Tag *, ::javax::swing::text::MutableAttributeSet *);
public: // actually package-private
  virtual void printBuffer();
public: // actually protected
  ::javax::swing::text::MutableAttributeSet * __attribute__((aligned(__alignof__( ::javax::swing::text::html::HTMLEditorKit$ParserCallback)))) charAttr;
  ::java::util::Vector * parseBuffer;
public: // actually package-private
  ::java::util::Stack * charAttrStack;
  ::java::util::Stack * parseStack;
  ::java::util::HashMap * tagToAction;
  jboolean endHTMLEncountered;
  jint popDepth;
  jint pushDepth;
  jint offset;
  ::javax::swing::text::html::HTML$Tag * insertTag;
  jboolean insertTagEncountered;
  jboolean debug;
  ::javax::swing::text::html::HTMLDocument * this$0;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_text_html_HTMLDocument$HTMLReader__
