
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __java_awt_geom_RoundRectangle2D$1__
#define __java_awt_geom_RoundRectangle2D$1__

#pragma interface

#include <java/lang/Object.h>
#include <gcj/array.h>

extern "Java"
{
  namespace java
  {
    namespace awt
    {
      namespace geom
      {
          class AffineTransform;
          class Arc2D;
          class PathIterator;
          class RoundRectangle2D;
          class RoundRectangle2D$1;
      }
    }
  }
}

class java::awt::geom::RoundRectangle2D$1 : public ::java::lang::Object
{

public: // actually package-private
  RoundRectangle2D$1(::java::awt::geom::RoundRectangle2D *, jdouble, jdouble, jdouble, jdouble, jdouble, jdouble, ::java::awt::geom::AffineTransform *);
public:
  jint getWindingRule();
  jboolean isDone();
private:
  void getPoint(jint);
public:
  void next();
  jint currentSegment(JArray< jfloat > *);
  jint currentSegment(JArray< jdouble > *);
private:
  jint __attribute__((aligned(__alignof__( ::java::lang::Object)))) current;
  ::java::awt::geom::PathIterator * corner;
  ::java::awt::geom::Arc2D * arc;
  JArray< jdouble > * temp;
public: // actually package-private
  ::java::awt::geom::RoundRectangle2D * this$0;
private:
  jdouble val$maxx;
  jdouble val$miny;
  jdouble val$archeight;
  jdouble val$maxy;
  jdouble val$arcwidth;
  jdouble val$minx;
  ::java::awt::geom::AffineTransform * val$at;
public:
  static ::java::lang::Class class$;
};

#endif // __java_awt_geom_RoundRectangle2D$1__
