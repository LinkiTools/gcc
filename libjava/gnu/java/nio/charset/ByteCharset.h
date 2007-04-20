
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_java_nio_charset_ByteCharset__
#define __gnu_java_nio_charset_ByteCharset__

#pragma interface

#include <java/nio/charset/Charset.h>
#include <gcj/array.h>

extern "Java"
{
  namespace gnu
  {
    namespace java
    {
      namespace nio
      {
        namespace charset
        {
            class ByteCharset;
        }
      }
    }
  }
  namespace java
  {
    namespace nio
    {
      namespace charset
      {
          class Charset;
          class CharsetDecoder;
          class CharsetEncoder;
      }
    }
  }
}

class gnu::java::nio::charset::ByteCharset : public ::java::nio::charset::Charset
{

public: // actually package-private
  ByteCharset(::java::lang::String *, JArray< ::java::lang::String * > *);
public:
  virtual jboolean contains(::java::nio::charset::Charset *);
public: // actually package-private
  virtual JArray< jchar > * getLookupTable();
public:
  virtual ::java::nio::charset::CharsetDecoder * newDecoder();
  virtual ::java::nio::charset::CharsetEncoder * newEncoder();
public: // actually protected
  JArray< jchar > * __attribute__((aligned(__alignof__( ::java::nio::charset::Charset)))) lookupTable;
  static const jchar NONE = 65533;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_java_nio_charset_ByteCharset__
