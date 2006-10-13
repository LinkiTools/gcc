
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __java_awt_EventQueue__
#define __java_awt_EventQueue__

#pragma interface

#include <java/lang/Object.h>
#include <gcj/array.h>

extern "Java"
{
  namespace java
  {
    namespace awt
    {
        class AWTEvent;
        class EventDispatchThread;
        class EventQueue;
    }
  }
}

class java::awt::EventQueue : public ::java::lang::Object
{

  void setShutdown(jboolean);
public: // actually package-private
  virtual jboolean isShutdown();
public:
  EventQueue();
  virtual ::java::awt::AWTEvent * getNextEvent();
  virtual ::java::awt::AWTEvent * peekEvent();
  virtual ::java::awt::AWTEvent * peekEvent(jint);
  virtual void postEvent(::java::awt::AWTEvent *);
  static void invokeAndWait(::java::lang::Runnable *);
  static void invokeLater(::java::lang::Runnable *);
  static jboolean isDispatchThread();
  static ::java::awt::AWTEvent * getCurrentEvent();
  virtual void push(::java::awt::EventQueue *);
public: // actually protected
  virtual void pop();
  virtual void dispatchEvent(::java::awt::AWTEvent *);
public:
  static jlong getMostRecentEventTime();
private:
  static const jint INITIAL_QUEUE_DEPTH = 8;
  JArray< ::java::awt::AWTEvent * > * __attribute__((aligned(__alignof__( ::java::lang::Object)))) queue;
  jint next_in;
  jint next_out;
  ::java::awt::EventQueue * next;
  ::java::awt::EventQueue * prev;
  ::java::awt::AWTEvent * currentEvent;
  jlong lastWhen;
  ::java::awt::EventDispatchThread * dispatchThread;
  jboolean shutdown;
public:
  static ::java::lang::Class class$;
};

#endif // __java_awt_EventQueue__
