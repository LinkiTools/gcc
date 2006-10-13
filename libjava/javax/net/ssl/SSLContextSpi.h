
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_net_ssl_SSLContextSpi__
#define __javax_net_ssl_SSLContextSpi__

#pragma interface

#include <java/lang/Object.h>
#include <gcj/array.h>

extern "Java"
{
  namespace java
  {
    namespace security
    {
        class SecureRandom;
    }
  }
  namespace javax
  {
    namespace net
    {
      namespace ssl
      {
          class KeyManager;
          class SSLContextSpi;
          class SSLServerSocketFactory;
          class SSLSessionContext;
          class SSLSocketFactory;
          class TrustManager;
      }
    }
  }
}

class javax::net::ssl::SSLContextSpi : public ::java::lang::Object
{

public:
  SSLContextSpi();
public: // actually protected
  virtual ::javax::net::ssl::SSLSessionContext * engineGetClientSessionContext() = 0;
  virtual ::javax::net::ssl::SSLSessionContext * engineGetServerSessionContext() = 0;
  virtual ::javax::net::ssl::SSLServerSocketFactory * engineGetServerSocketFactory() = 0;
  virtual ::javax::net::ssl::SSLSocketFactory * engineGetSocketFactory() = 0;
  virtual void engineInit(JArray< ::javax::net::ssl::KeyManager * > *, JArray< ::javax::net::ssl::TrustManager * > *, ::java::security::SecureRandom *) = 0;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_net_ssl_SSLContextSpi__
