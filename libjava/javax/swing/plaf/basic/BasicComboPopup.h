
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_swing_plaf_basic_BasicComboPopup__
#define __javax_swing_plaf_basic_BasicComboPopup__

#pragma interface

#include <javax/swing/JPopupMenu.h>
extern "Java"
{
  namespace java
  {
    namespace awt
    {
        class Rectangle;
      namespace event
      {
          class ItemListener;
          class KeyListener;
          class MouseEvent;
          class MouseListener;
          class MouseMotionListener;
      }
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
        class ComboBoxModel;
        class JComboBox;
        class JList;
        class JScrollPane;
        class Timer;
      namespace event
      {
          class ListDataListener;
          class ListSelectionListener;
      }
      namespace plaf
      {
        namespace basic
        {
            class BasicComboPopup;
        }
      }
    }
  }
}

class javax::swing::plaf::basic::BasicComboPopup : public ::javax::swing::JPopupMenu
{

public:
  BasicComboPopup(::javax::swing::JComboBox *);
  virtual void show();
  virtual void hide();
  virtual ::javax::swing::JList * getList();
  virtual ::java::awt::event::MouseListener * getMouseListener();
  virtual ::java::awt::event::MouseMotionListener * getMouseMotionListener();
  virtual ::java::awt::event::KeyListener * getKeyListener();
  virtual void uninstallingUI();
public: // actually protected
  virtual void uninstallComboBoxModelListeners(::javax::swing::ComboBoxModel *);
  virtual void uninstallKeyboardActions();
  virtual void firePopupMenuWillBecomeVisible();
  virtual void firePopupMenuWillBecomeInvisible();
  virtual void firePopupMenuCanceled();
  virtual ::java::awt::event::MouseListener * createMouseListener();
  virtual ::java::awt::event::MouseMotionListener * createMouseMotionListener();
  virtual ::java::awt::event::KeyListener * createKeyListener();
  virtual ::javax::swing::event::ListSelectionListener * createListSelectionListener();
  virtual ::javax::swing::event::ListDataListener * createListDataListener();
  virtual ::java::awt::event::MouseListener * createListMouseListener();
  virtual ::java::awt::event::MouseMotionListener * createListMouseMotionListener();
  virtual ::java::beans::PropertyChangeListener * createPropertyChangeListener();
  virtual ::java::awt::event::ItemListener * createItemListener();
  virtual ::javax::swing::JList * createList();
  virtual void configureList();
  virtual void installListListeners();
  virtual ::javax::swing::JScrollPane * createScroller();
  virtual void configureScroller();
  virtual void configurePopup();
  virtual void installComboBoxListeners();
  virtual void installComboBoxModelListeners(::javax::swing::ComboBoxModel *);
  virtual void installKeyboardActions();
public:
  virtual jboolean isFocusTraversable();
public: // actually protected
  virtual void startAutoScrolling(jint);
  virtual void stopAutoScrolling();
  virtual void autoScrollUp();
  virtual void autoScrollDown();
  virtual void delegateFocus(::java::awt::event::MouseEvent *);
  virtual void togglePopup();
  virtual ::java::awt::event::MouseEvent * convertMouseEvent(::java::awt::event::MouseEvent *);
  virtual jint getPopupHeightForRowCount(jint);
  virtual ::java::awt::Rectangle * computePopupBounds(jint, jint, jint, jint);
  virtual void updateListBoxSelectionForEvent(::java::awt::event::MouseEvent *, jboolean);
private:
  void uninstallListeners();
  void uninstallListListeners();
  void uninstallComboBoxListeners();
public: // actually package-private
  virtual void syncListSelection();
public: // actually protected
  ::javax::swing::Timer * __attribute__((aligned(__alignof__( ::javax::swing::JPopupMenu)))) autoscrollTimer;
  ::javax::swing::JComboBox * comboBox;
  jboolean hasEntered;
  jboolean isAutoScrolling;
  ::java::awt::event::ItemListener * itemListener;
  ::java::awt::event::KeyListener * keyListener;
  ::javax::swing::JList * list;
  ::javax::swing::event::ListDataListener * listDataListener;
  ::java::awt::event::MouseListener * listMouseListener;
  ::java::awt::event::MouseMotionListener * listMouseMotionListener;
  ::javax::swing::event::ListSelectionListener * listSelectionListener;
  ::java::awt::event::MouseListener * mouseListener;
  ::java::awt::event::MouseMotionListener * mouseMotionListener;
  ::java::beans::PropertyChangeListener * propertyChangeListener;
  static const jint SCROLL_DOWN = 1;
  static const jint SCROLL_UP = 0;
  jint scrollDirection;
  ::javax::swing::JScrollPane * scroller;
  jboolean valueIsAdjusting;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_swing_plaf_basic_BasicComboPopup__
