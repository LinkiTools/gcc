
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __java_security_spec_DSAPrivateKeySpec__
#define __java_security_spec_DSAPrivateKeySpec__

#pragma interface

#include <java/lang/Object.h>
extern "Java"
{
  namespace java
  {
    namespace math
    {
        class BigInteger;
    }
    namespace security
    {
      namespace spec
      {
          class DSAPrivateKeySpec;
      }
    }
  }
}

class java::security::spec::DSAPrivateKeySpec : public ::java::lang::Object
{

public:
  DSAPrivateKeySpec(::java::math::BigInteger *, ::java::math::BigInteger *, ::java::math::BigInteger *, ::java::math::BigInteger *);
  virtual ::java::math::BigInteger * getX();
  virtual ::java::math::BigInteger * getP();
  virtual ::java::math::BigInteger * getQ();
  virtual ::java::math::BigInteger * getG();
private:
  ::java::math::BigInteger * __attribute__((aligned(__alignof__( ::java::lang::Object)))) x;
  ::java::math::BigInteger * p;
  ::java::math::BigInteger * q;
  ::java::math::BigInteger * g;
public:
  static ::java::lang::Class class$;
};

#endif // __java_security_spec_DSAPrivateKeySpec__
