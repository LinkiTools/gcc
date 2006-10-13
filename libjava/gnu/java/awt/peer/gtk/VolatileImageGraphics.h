
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_java_awt_peer_gtk_VolatileImageGraphics__
#define __gnu_java_awt_peer_gtk_VolatileImageGraphics__

#pragma interface

#include <gnu/java/awt/peer/gtk/ComponentGraphics.h>
extern "Java"
{
  namespace gnu
  {
    namespace java
    {
      namespace awt
      {
        namespace peer
        {
          namespace gtk
          {
              class GtkVolatileImage;
              class VolatileImageGraphics;
          }
        }
      }
    }
  }
  namespace java
  {
    namespace awt
    {
        class Graphics;
        class GraphicsConfiguration;
        class Image;
      namespace geom
      {
          class Rectangle2D;
      }
      namespace image
      {
          class ImageObserver;
      }
    }
  }
}

class gnu::java::awt::peer::gtk::VolatileImageGraphics : public ::gnu::java::awt::peer::gtk::ComponentGraphics
{

public:
  VolatileImageGraphics(::gnu::java::awt::peer::gtk::GtkVolatileImage *);
private:
  VolatileImageGraphics(::gnu::java::awt::peer::gtk::VolatileImageGraphics *);
public:
  virtual void copyAreaImpl(jint, jint, jint, jint, jint, jint);
  virtual ::java::awt::GraphicsConfiguration * getDeviceConfiguration();
  virtual ::java::awt::Graphics * create();
  virtual jboolean drawImage(::java::awt::Image *, jint, jint, ::java::awt::image::ImageObserver *);
  virtual jboolean drawImage(::java::awt::Image *, jint, jint, jint, jint, ::java::awt::image::ImageObserver *);
public: // actually protected
  virtual ::java::awt::geom::Rectangle2D * getRealBounds();
private:
  ::gnu::java::awt::peer::gtk::GtkVolatileImage * __attribute__((aligned(__alignof__( ::gnu::java::awt::peer::gtk::ComponentGraphics)))) owner;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_java_awt_peer_gtk_VolatileImageGraphics__
