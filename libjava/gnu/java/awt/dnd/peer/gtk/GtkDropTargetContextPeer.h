
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_java_awt_dnd_peer_gtk_GtkDropTargetContextPeer__
#define __gnu_java_awt_dnd_peer_gtk_GtkDropTargetContextPeer__

#pragma interface

#include <gnu/java/awt/peer/gtk/GtkGenericPeer.h>
#include <gcj/array.h>

extern "Java"
{
  namespace gnu
  {
    namespace java
    {
      namespace awt
      {
        namespace dnd
        {
          namespace peer
          {
            namespace gtk
            {
                class GtkDropTargetContextPeer;
            }
          }
        }
      }
    }
  }
  namespace java
  {
    namespace awt
    {
      namespace datatransfer
      {
          class DataFlavor;
          class Transferable;
      }
      namespace dnd
      {
          class DropTarget;
      }
    }
  }
}

class gnu::java::awt::dnd::peer::gtk::GtkDropTargetContextPeer : public ::gnu::java::awt::peer::gtk::GtkGenericPeer
{

public:
  GtkDropTargetContextPeer();
  virtual void setTargetActions(jint);
  virtual jint getTargetActions();
  virtual ::java::awt::dnd::DropTarget * getDropTarget();
  virtual JArray< ::java::awt::datatransfer::DataFlavor * > * getTransferDataFlavors();
  virtual ::java::awt::datatransfer::Transferable * getTransferable();
  virtual jboolean isTransferableJVMLocal();
  virtual void acceptDrag(jint);
  virtual void rejectDrag();
  virtual void acceptDrop(jint);
  virtual void rejectDrop();
  virtual void dropComplete(jboolean);
  static ::java::lang::Class class$;
};

#endif // __gnu_java_awt_dnd_peer_gtk_GtkDropTargetContextPeer__
