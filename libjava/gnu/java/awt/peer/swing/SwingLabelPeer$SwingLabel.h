
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_java_awt_peer_swing_SwingLabelPeer$SwingLabel__
#define __gnu_java_awt_peer_swing_SwingLabelPeer$SwingLabel__

#pragma interface

#include <javax/swing/JLabel.h>
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
          namespace swing
          {
              class SwingLabelPeer;
              class SwingLabelPeer$SwingLabel;
          }
        }
      }
    }
  }
  namespace java
  {
    namespace awt
    {
        class Image;
        class Point;
      namespace event
      {
          class KeyEvent;
          class MouseEvent;
      }
    }
  }
  namespace javax
  {
    namespace swing
    {
        class JComponent;
    }
  }
}

class gnu::java::awt::peer::swing::SwingLabelPeer$SwingLabel : public ::javax::swing::JLabel
{

  SwingLabelPeer$SwingLabel(::gnu::java::awt::peer::swing::SwingLabelPeer *);
public:
  virtual ::javax::swing::JComponent * getJComponent();
  virtual void handleMouseEvent(::java::awt::event::MouseEvent *);
  virtual void handleMouseMotionEvent(::java::awt::event::MouseEvent *);
  virtual void handleKeyEvent(::java::awt::event::KeyEvent *);
  virtual ::java::awt::Point * getLocationOnScreen();
  virtual jboolean isShowing();
  virtual ::java::awt::Image * createImage(jint, jint);
public: // actually package-private
  SwingLabelPeer$SwingLabel(::gnu::java::awt::peer::swing::SwingLabelPeer *, ::gnu::java::awt::peer::swing::SwingLabelPeer$SwingLabel *);
  ::gnu::java::awt::peer::swing::SwingLabelPeer * __attribute__((aligned(__alignof__( ::javax::swing::JLabel)))) this$0;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_java_awt_peer_swing_SwingLabelPeer$SwingLabel__
