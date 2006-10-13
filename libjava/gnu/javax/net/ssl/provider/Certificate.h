
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_javax_net_ssl_provider_Certificate__
#define __gnu_javax_net_ssl_provider_Certificate__

#pragma interface

#include <java/lang/Object.h>
#include <gcj/array.h>

extern "Java"
{
  namespace gnu
  {
    namespace javax
    {
      namespace net
      {
        namespace ssl
        {
          namespace provider
          {
              class Certificate;
              class CertificateType;
          }
        }
      }
    }
  }
  namespace java
  {
    namespace security
    {
      namespace cert
      {
          class X509Certificate;
      }
    }
  }
}

class gnu::javax::net::ssl::provider::Certificate : public ::java::lang::Object
{

public: // actually package-private
  Certificate(JArray< ::java::security::cert::X509Certificate * > *);
  static ::gnu::javax::net::ssl::provider::Certificate * read(::java::io::InputStream *, ::gnu::javax::net::ssl::provider::CertificateType *);
public:
  void write(::java::io::OutputStream *);
public: // actually package-private
  JArray< ::java::security::cert::X509Certificate * > * getCertificates();
public:
  ::java::lang::String * toString();
private:
  JArray< ::java::security::cert::X509Certificate * > * __attribute__((aligned(__alignof__( ::java::lang::Object)))) certs;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_javax_net_ssl_provider_Certificate__
