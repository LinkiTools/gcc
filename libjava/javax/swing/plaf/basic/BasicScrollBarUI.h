
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_plaf_basic_BasicScrollBarUI__
#define __javax_swing_plaf_basic_BasicScrollBarUI__

#pragma interface

#include <javax/swing/plaf/ScrollBarUI.h>
extern "Java"
{
  namespace java
  {
    namespace awt
    {
        class Color;
        class Component;
        class Container;
        class Dimension;
        class Graphics;
        class Rectangle;
    }
    namespace beans
    {
        class PropertyChangeListener;
    }
  }
  namespace javax
  {
    namespace swing
    {
        class ActionMap;
        class InputMap;
        class JButton;
        class JComponent;
        class JScrollBar;
        class Timer;
      namespace plaf
      {
          class ComponentUI;
        namespace basic
        {
            class BasicScrollBarUI;
            class BasicScrollBarUI$ArrowButtonListener;
            class BasicScrollBarUI$ModelListener;
            class BasicScrollBarUI$ScrollListener;
            class BasicScrollBarUI$TrackListener;
        }
      }
    }
  }
}

class javax::swing::plaf::basic::BasicScrollBarUI : public ::javax::swing::plaf::ScrollBarUI
{

public:
  BasicScrollBarUI();
  virtual void addLayoutComponent(::java::lang::String *, ::java::awt::Component *);
public: // actually protected
  virtual void configureScrollBarColors();
  virtual ::javax::swing::plaf::basic::BasicScrollBarUI$ArrowButtonListener * createArrowButtonListener();
  virtual ::javax::swing::JButton * createIncreaseButton(jint);
  virtual ::javax::swing::JButton * createDecreaseButton(jint);
  virtual ::javax::swing::plaf::basic::BasicScrollBarUI$ModelListener * createModelListener();
  virtual ::java::beans::PropertyChangeListener * createPropertyChangeListener();
  virtual ::javax::swing::plaf::basic::BasicScrollBarUI$ScrollListener * createScrollListener();
  virtual ::javax::swing::plaf::basic::BasicScrollBarUI$TrackListener * createTrackListener();
public:
  static ::javax::swing::plaf::ComponentUI * createUI(::javax::swing::JComponent *);
  virtual ::java::awt::Dimension * getMaximumSize(::javax::swing::JComponent *);
public: // actually protected
  virtual ::java::awt::Dimension * getMaximumThumbSize();
public:
  virtual ::java::awt::Dimension * getMinimumSize(::javax::swing::JComponent *);
public: // actually protected
  virtual ::java::awt::Dimension * getMinimumThumbSize();
public: // actually package-private
  virtual void calculatePreferredSize();
public:
  virtual ::java::awt::Dimension * getPreferredSize(::javax::swing::JComponent *);
public: // actually protected
  virtual ::java::awt::Rectangle * getThumbBounds();
  virtual ::java::awt::Rectangle * getTrackBounds();
  virtual void installComponents();
  virtual void installDefaults();
  virtual void installKeyboardActions();
  virtual void uninstallKeyboardActions();
public: // actually package-private
  virtual ::javax::swing::InputMap * getInputMap(jint);
  virtual ::javax::swing::ActionMap * getActionMap();
  virtual ::javax::swing::ActionMap * createActionMap();
public: // actually protected
  virtual void installListeners();
public:
  virtual void installUI(::javax::swing::JComponent *);
  virtual void layoutContainer(::java::awt::Container *);
public: // actually protected
  virtual void layoutHScrollbar(::javax::swing::JScrollBar *);
  virtual void layoutVScrollbar(::javax::swing::JScrollBar *);
public: // actually package-private
  virtual void updateThumbRect();
public:
  virtual ::java::awt::Dimension * minimumLayoutSize(::java::awt::Container *);
  virtual void paint(::java::awt::Graphics *, ::javax::swing::JComponent *);
public: // actually protected
  virtual void paintDecreaseHighlight(::java::awt::Graphics *);
  virtual void paintIncreaseHighlight(::java::awt::Graphics *);
  virtual void paintThumb(::java::awt::Graphics *, ::javax::swing::JComponent *, ::java::awt::Rectangle *);
  virtual void paintTrack(::java::awt::Graphics *, ::javax::swing::JComponent *, ::java::awt::Rectangle *);
public:
  virtual ::java::awt::Dimension * preferredLayoutSize(::java::awt::Container *);
  virtual void removeLayoutComponent(::java::awt::Component *);
public: // actually protected
  virtual void scrollByBlock(jint);
  virtual void scrollByUnit(jint);
  virtual void setThumbBounds(jint, jint, jint, jint);
  virtual void uninstallComponents();
  virtual void uninstallDefaults();
  virtual void uninstallListeners();
public:
  virtual void uninstallUI(::javax::swing::JComponent *);
public: // actually package-private
  virtual jint valueForYPosition(jint);
  virtual jint valueForXPosition(jint);
public:
  virtual jboolean isThumbRollover();
public: // actually protected
  virtual void setThumbRollover(jboolean);
public:
  virtual jboolean getSupportsAbsolutePositioning();
public: // actually protected
  ::javax::swing::plaf::basic::BasicScrollBarUI$ArrowButtonListener * __attribute__((aligned(__alignof__( ::javax::swing::plaf::ScrollBarUI)))) buttonListener;
  ::javax::swing::plaf::basic::BasicScrollBarUI$ModelListener * modelListener;
  ::java::beans::PropertyChangeListener * propertyChangeListener;
  ::javax::swing::plaf::basic::BasicScrollBarUI$ScrollListener * scrollListener;
  ::javax::swing::plaf::basic::BasicScrollBarUI$TrackListener * trackListener;
  ::javax::swing::JButton * decrButton;
  ::javax::swing::JButton * incrButton;
  ::java::awt::Dimension * maximumThumbSize;
  ::java::awt::Dimension * minimumThumbSize;
  ::java::awt::Color * thumbColor;
  ::java::awt::Color * thumbDarkShadowColor;
  ::java::awt::Color * thumbHighlightColor;
  ::java::awt::Color * thumbLightShadowColor;
  ::java::awt::Color * trackHighlightColor;
  ::java::awt::Color * trackColor;
  ::java::awt::Rectangle * trackRect;
  ::java::awt::Rectangle * thumbRect;
  static const jint DECREASE_HIGHLIGHT = 1;
  static const jint INCREASE_HIGHLIGHT = 2;
  static const jint NO_HIGHLIGHT = 0;
private:
  static const jint POSITIVE_SCROLL = 1;
  static const jint NEGATIVE_SCROLL = -1;
  ::java::awt::Dimension * preferredSize;
public: // actually protected
  jint trackHighlight;
  jboolean isDragging;
  ::javax::swing::Timer * scrollTimer;
  ::javax::swing::JScrollBar * scrollbar;
public: // actually package-private
  jboolean thumbRollover;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_plaf_basic_BasicScrollBarUI__
