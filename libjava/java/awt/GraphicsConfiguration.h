
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __java_awt_GraphicsConfiguration__
#define __java_awt_GraphicsConfiguration__

#pragma interface

#include <java/lang/Object.h>
extern "Java"
{
  namespace java
  {
    namespace awt
    {
        class BufferCapabilities;
        class GraphicsConfiguration;
        class GraphicsDevice;
        class ImageCapabilities;
        class Rectangle;
      namespace geom
      {
          class AffineTransform;
      }
      namespace image
      {
          class BufferedImage;
          class ColorModel;
          class VolatileImage;
      }
    }
  }
}

class java::awt::GraphicsConfiguration : public ::java::lang::Object
{

public: // actually protected
  GraphicsConfiguration();
public:
  virtual ::java::awt::GraphicsDevice * getDevice() = 0;
  virtual ::java::awt::image::BufferedImage * createCompatibleImage(jint, jint) = 0;
  virtual ::java::awt::image::VolatileImage * createCompatibleVolatileImage(jint, jint) = 0;
  virtual ::java::awt::image::VolatileImage * createCompatibleVolatileImage(jint, jint, ::java::awt::ImageCapabilities *);
  virtual ::java::awt::image::VolatileImage * createCompatibleVolatileImage(jint, jint, jint) = 0;
  virtual ::java::awt::image::BufferedImage * createCompatibleImage(jint, jint, jint) = 0;
  virtual ::java::awt::image::ColorModel * getColorModel() = 0;
  virtual ::java::awt::image::ColorModel * getColorModel(jint) = 0;
  virtual ::java::awt::geom::AffineTransform * getDefaultTransform() = 0;
  virtual ::java::awt::geom::AffineTransform * getNormalizingTransform() = 0;
  virtual ::java::awt::Rectangle * getBounds() = 0;
  virtual ::java::awt::BufferCapabilities * getBufferCapabilities();
  virtual ::java::awt::ImageCapabilities * getImageCapabilities();
private:
  ::java::awt::ImageCapabilities * __attribute__((aligned(__alignof__( ::java::lang::Object)))) imageCapabilities;
  ::java::awt::BufferCapabilities * bufferCapabilities;
public:
  static ::java::lang::Class class$;
};

#endif // __java_awt_GraphicsConfiguration__
