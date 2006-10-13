
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_java_awt_peer_gtk_GtkFramePeer__
#define __gnu_java_awt_peer_gtk_GtkFramePeer__

#pragma interface

#include <gnu/java/awt/peer/gtk/GtkWindowPeer.h>
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
              class GtkFramePeer;
              class GtkImage;
          }
        }
      }
    }
  }
  namespace java
  {
    namespace awt
    {
        class Frame;
        class Image;
        class MenuBar;
        class Rectangle;
      namespace peer
      {
          class MenuBarPeer;
      }
    }
  }
}

class gnu::java::awt::peer::gtk::GtkFramePeer : public ::gnu::java::awt::peer::gtk::GtkWindowPeer
{

public: // actually package-private
  virtual jint getMenuBarHeight(::java::awt::peer::MenuBarPeer *);
  virtual void setMenuBarWidthUnlocked(::java::awt::peer::MenuBarPeer *, jint);
  virtual void setMenuBarWidth(::java::awt::peer::MenuBarPeer *, jint);
  virtual void setMenuBarPeer(::java::awt::peer::MenuBarPeer *);
  virtual void removeMenuBarPeer();
  virtual void gtkFixedSetVisible(jboolean);
  virtual jint getMenuBarHeight();
public:
  virtual void setMenuBar(::java::awt::MenuBar *);
  virtual void setBounds(jint, jint, jint, jint);
  virtual void setResizable(jboolean);
public: // actually protected
  virtual void postInsetsChangedEvent(jint, jint, jint, jint);
public:
  GtkFramePeer(::java::awt::Frame *);
public: // actually package-private
  virtual void create();
  virtual void nativeSetIconImage(::gnu::java::awt::peer::gtk::GtkImage *);
public:
  virtual void setIconImage(::java::awt::Image *);
public: // actually protected
  virtual void postConfigureEvent(jint, jint, jint, jint);
public:
  virtual jint getState();
  virtual void setState(jint);
  virtual void setMaximizedBounds(::java::awt::Rectangle *);
  virtual void setBoundsPrivate(jint, jint, jint, jint);
  virtual jboolean requestWindowFocus();
private:
  jint __attribute__((aligned(__alignof__( ::gnu::java::awt::peer::gtk::GtkWindowPeer)))) menuBarHeight;
  ::java::awt::peer::MenuBarPeer * menuBar;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_java_awt_peer_gtk_GtkFramePeer__
