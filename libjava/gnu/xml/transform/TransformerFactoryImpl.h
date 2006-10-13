
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_xml_transform_TransformerFactoryImpl__
#define __gnu_xml_transform_TransformerFactoryImpl__

#pragma interface

#include <javax/xml/transform/TransformerFactory.h>
#include <gcj/array.h>

extern "Java"
{
  namespace gnu
  {
    namespace xml
    {
      namespace transform
      {
          class Stylesheet;
          class TransformerFactoryImpl;
          class XSLURIResolver;
      }
    }
  }
  namespace javax
  {
    namespace xml
    {
      namespace transform
      {
          class ErrorListener;
          class Source;
          class Templates;
          class Transformer;
          class URIResolver;
      }
      namespace xpath
      {
          class XPathFactory;
      }
    }
  }
}

class gnu::xml::transform::TransformerFactoryImpl : public ::javax::xml::transform::TransformerFactory
{

public:
  TransformerFactoryImpl();
  virtual ::javax::xml::transform::Transformer * newTransformer(::javax::xml::transform::Source *);
  virtual ::javax::xml::transform::Transformer * newTransformer();
  virtual ::javax::xml::transform::Templates * newTemplates(::javax::xml::transform::Source *);
public: // actually package-private
  virtual ::gnu::xml::transform::Stylesheet * newStylesheet(::javax::xml::transform::Source *, jint, ::gnu::xml::transform::Stylesheet *);
public:
  virtual ::javax::xml::transform::Source * getAssociatedStylesheet(::javax::xml::transform::Source *, ::java::lang::String *, ::java::lang::String *, ::java::lang::String *);
public: // actually package-private
  virtual ::java::util::Map * parseParameters(::java::lang::String *);
  virtual ::java::lang::String * unquote(::java::lang::String *);
public:
  virtual void setURIResolver(::javax::xml::transform::URIResolver *);
  virtual ::javax::xml::transform::URIResolver * getURIResolver();
  virtual void setFeature(::java::lang::String *, jboolean);
  virtual jboolean getFeature(::java::lang::String *);
  virtual void setAttribute(::java::lang::String *, ::java::lang::Object *);
  virtual ::java::lang::Object * getAttribute(::java::lang::String *);
  virtual void setErrorListener(::javax::xml::transform::ErrorListener *);
  virtual ::javax::xml::transform::ErrorListener * getErrorListener();
  static void main(JArray< ::java::lang::String * > *);
public: // actually package-private
  ::javax::xml::xpath::XPathFactory * __attribute__((aligned(__alignof__( ::javax::xml::transform::TransformerFactory)))) xpathFactory;
  ::gnu::xml::transform::XSLURIResolver * resolver;
  ::javax::xml::transform::ErrorListener * userListener;
  ::javax::xml::transform::URIResolver * userResolver;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_xml_transform_TransformerFactoryImpl__
