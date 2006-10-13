
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_plaf_basic_BasicMenuItemUI__
#define __javax_swing_plaf_basic_BasicMenuItemUI__

#pragma interface

#include <javax/swing/plaf/MenuItemUI.h>
#include <gcj/array.h>

extern "Java"
{
  namespace java
  {
    namespace awt
    {
        class Color;
        class Dimension;
        class Font;
        class FontMetrics;
        class Graphics;
        class Rectangle;
      namespace event
      {
          class ItemListener;
      }
    }
  }
  namespace javax
  {
    namespace swing
    {
        class Icon;
        class JComponent;
        class JMenuItem;
        class KeyStroke;
        class MenuElement;
        class MenuSelectionManager;
      namespace event
      {
          class MenuDragMouseListener;
          class MenuKeyListener;
          class MouseInputListener;
      }
      namespace plaf
      {
          class ComponentUI;
        namespace basic
        {
            class BasicMenuItemUI;
            class BasicMenuItemUI$PropertyChangeHandler;
        }
      }
    }
  }
}

class javax::swing::plaf::basic::BasicMenuItemUI : public ::javax::swing::plaf::MenuItemUI
{

public:
  BasicMenuItemUI();
public: // actually protected
  virtual ::javax::swing::event::MenuDragMouseListener * createMenuDragMouseListener(::javax::swing::JComponent *);
  virtual ::javax::swing::event::MenuKeyListener * createMenuKeyListener(::javax::swing::JComponent *);
  virtual ::javax::swing::event::MouseInputListener * createMouseInputListener(::javax::swing::JComponent *);
public:
  static ::javax::swing::plaf::ComponentUI * createUI(::javax::swing::JComponent *);
public: // actually protected
  virtual void doClick(::javax::swing::MenuSelectionManager *);
public:
  virtual ::java::awt::Dimension * getMaximumSize(::javax::swing::JComponent *);
  virtual ::java::awt::Dimension * getMinimumSize(::javax::swing::JComponent *);
  virtual JArray< ::javax::swing::MenuElement * > * getPath();
public: // actually protected
  virtual ::java::awt::Dimension * getPreferredMenuItemSize(::javax::swing::JComponent *, ::javax::swing::Icon *, ::javax::swing::Icon *, jint);
public:
  virtual ::java::awt::Dimension * getPreferredSize(::javax::swing::JComponent *);
public: // actually protected
  virtual ::java::lang::String * getPropertyPrefix();
  virtual void installComponents(::javax::swing::JMenuItem *);
  virtual void installDefaults();
  virtual void installKeyboardActions();
  virtual void installListeners();
public:
  virtual void installUI(::javax::swing::JComponent *);
  virtual void paint(::java::awt::Graphics *, ::javax::swing::JComponent *);
public: // actually protected
  virtual void paintBackground(::java::awt::Graphics *, ::javax::swing::JMenuItem *, ::java::awt::Color *);
  virtual void paintMenuItem(::java::awt::Graphics *, ::javax::swing::JComponent *, ::javax::swing::Icon *, ::javax::swing::Icon *, ::java::awt::Color *, ::java::awt::Color *, jint);
  virtual void paintText(::java::awt::Graphics *, ::javax::swing::JMenuItem *, ::java::awt::Rectangle *, ::java::lang::String *);
  virtual void uninstallComponents(::javax::swing::JMenuItem *);
  virtual void uninstallDefaults();
  virtual void uninstallKeyboardActions();
  virtual void uninstallListeners();
public:
  virtual void uninstallUI(::javax::swing::JComponent *);
  virtual void update(::java::awt::Graphics *, ::javax::swing::JComponent *);
private:
  ::java::lang::String * getAcceleratorText(::javax::swing::KeyStroke *);
  ::java::awt::Rectangle * getAcceleratorRect(::javax::swing::KeyStroke *, ::java::awt::FontMetrics *);
  ::java::lang::String * getAcceleratorString(::javax::swing::JMenuItem *);
  void layoutMenuItem(::javax::swing::JMenuItem *, ::java::lang::String *);
public: // actually protected
  ::java::awt::Font * __attribute__((aligned(__alignof__( ::javax::swing::plaf::MenuItemUI)))) acceleratorFont;
  ::java::awt::Color * acceleratorForeground;
  ::java::awt::Color * acceleratorSelectionForeground;
  ::javax::swing::Icon * arrowIcon;
  ::javax::swing::Icon * checkIcon;
  jint defaultTextIconGap;
  ::java::awt::Color * disabledForeground;
  ::javax::swing::event::MenuDragMouseListener * menuDragMouseListener;
  ::javax::swing::JMenuItem * menuItem;
  ::javax::swing::event::MenuKeyListener * menuKeyListener;
  ::javax::swing::event::MouseInputListener * mouseInputListener;
  jboolean oldBorderPainted;
  ::java::awt::Color * selectionBackground;
  ::java::awt::Color * selectionForeground;
private:
  ::java::lang::String * acceleratorDelimiter;
  ::java::awt::event::ItemListener * itemListener;
  jint defaultAcceleratorLabelGap;
  jint MenuGap;
public: // actually package-private
  ::javax::swing::plaf::basic::BasicMenuItemUI$PropertyChangeHandler * propertyChangeListener;
private:
  ::java::awt::Rectangle * viewRect;
  ::java::awt::Rectangle * textRect;
  ::java::awt::Rectangle * accelRect;
  ::java::awt::Rectangle * iconRect;
  ::java::awt::Rectangle * arrowIconRect;
  ::java::awt::Rectangle * checkIconRect;
  ::java::awt::Rectangle * cachedRect;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_plaf_basic_BasicMenuItemUI__
