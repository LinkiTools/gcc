
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __java_beans_beancontext_BeanContextSupport__
#define __java_beans_beancontext_BeanContextSupport__

#pragma interface

#include <java/beans/beancontext/BeanContextChildSupport.h>
#include <gcj/array.h>

extern "Java"
{
  namespace java
  {
    namespace beans
    {
        class PropertyChangeEvent;
        class PropertyChangeListener;
        class VetoableChangeListener;
        class Visibility;
      namespace beancontext
      {
          class BeanContext;
          class BeanContextChild;
          class BeanContextMembershipEvent;
          class BeanContextMembershipListener;
          class BeanContextSupport;
          class BeanContextSupport$BCSChild;
      }
    }
    namespace net
    {
        class URL;
    }
  }
}

class java::beans::beancontext::BeanContextSupport : public ::java::beans::beancontext::BeanContextChildSupport
{

  void readObject(::java::io::ObjectInputStream *);
  void writeObject(::java::io::ObjectOutputStream *);
public:
  BeanContextSupport();
  BeanContextSupport(::java::beans::beancontext::BeanContext *);
  BeanContextSupport(::java::beans::beancontext::BeanContext *, ::java::util::Locale *);
  BeanContextSupport(::java::beans::beancontext::BeanContext *, ::java::util::Locale *, jboolean);
  BeanContextSupport(::java::beans::beancontext::BeanContext *, ::java::util::Locale *, jboolean, jboolean);
  virtual jboolean add(::java::lang::Object *);
  virtual jboolean addAll(::java::util::Collection *);
  virtual void addBeanContextMembershipListener(::java::beans::beancontext::BeanContextMembershipListener *);
  virtual jboolean avoidingGui();
public: // actually protected
  virtual ::java::util::Iterator * bcsChildren();
  virtual void bcsPreDeserializationHook(::java::io::ObjectInputStream *);
  virtual void bcsPreSerializationHook(::java::io::ObjectOutputStream *);
  virtual void childDeserializedHook(::java::lang::Object *, ::java::beans::beancontext::BeanContextSupport$BCSChild *);
  virtual void childJustAddedHook(::java::lang::Object *, ::java::beans::beancontext::BeanContextSupport$BCSChild *);
  virtual void childJustRemovedHook(::java::lang::Object *, ::java::beans::beancontext::BeanContextSupport$BCSChild *);
  static jboolean classEquals(::java::lang::Class *, ::java::lang::Class *);
public:
  virtual void clear();
  virtual jboolean contains(::java::lang::Object *);
  virtual jboolean containsAll(::java::util::Collection *);
  virtual jboolean containsKey(::java::lang::Object *);
public: // actually protected
  virtual JArray< ::java::lang::Object * > * copyChildren();
  virtual ::java::beans::beancontext::BeanContextSupport$BCSChild * createBCSChild(::java::lang::Object *, ::java::lang::Object *);
  virtual void deserialize(::java::io::ObjectInputStream *, ::java::util::Collection *);
public:
  virtual void dontUseGui();
public: // actually protected
  virtual void fireChildrenAdded(::java::beans::beancontext::BeanContextMembershipEvent *);
  virtual void fireChildrenRemoved(::java::beans::beancontext::BeanContextMembershipEvent *);
public:
  virtual ::java::beans::beancontext::BeanContext * getBeanContextPeer();
public: // actually protected
  static ::java::beans::beancontext::BeanContextChild * getChildBeanContextChild(::java::lang::Object *);
  static ::java::beans::beancontext::BeanContextMembershipListener * getChildBeanContextMembershipListener(::java::lang::Object *);
  static ::java::beans::PropertyChangeListener * getChildPropertyChangeListener(::java::lang::Object *);
  static ::java::io::Serializable * getChildSerializable(::java::lang::Object *);
  static ::java::beans::VetoableChangeListener * getChildVetoableChangeListener(::java::lang::Object *);
  static ::java::beans::Visibility * getChildVisibility(::java::lang::Object *);
public:
  virtual ::java::util::Locale * getLocale();
  virtual ::java::net::URL * getResource(::java::lang::String *, ::java::beans::beancontext::BeanContextChild *);
  virtual ::java::io::InputStream * getResourceAsStream(::java::lang::String *, ::java::beans::beancontext::BeanContextChild *);
public: // actually protected
  virtual void initialize();
public:
  virtual ::java::lang::Object * instantiateChild(::java::lang::String *);
  virtual jboolean isDesignTime();
  virtual jboolean isEmpty();
  virtual jboolean isSerializing();
  virtual ::java::util::Iterator * iterator();
  virtual jboolean needsGui();
  virtual void okToUseGui();
  virtual void propertyChange(::java::beans::PropertyChangeEvent *);
  virtual void readChildren(::java::io::ObjectInputStream *);
  virtual jboolean remove(::java::lang::Object *);
public: // actually protected
  virtual jboolean remove(::java::lang::Object *, jboolean);
public:
  virtual jboolean removeAll(::java::util::Collection *);
  virtual void removeBeanContextMembershipListener(::java::beans::beancontext::BeanContextMembershipListener *);
  virtual jboolean retainAll(::java::util::Collection *);
public: // actually protected
  virtual void serialize(::java::io::ObjectOutputStream *, ::java::util::Collection *);
public:
  virtual void setDesignTime(jboolean);
  virtual void setLocale(::java::util::Locale *);
  virtual jint size();
  virtual JArray< ::java::lang::Object * > * toArray();
  virtual JArray< ::java::lang::Object * > * toArray(JArray< ::java::lang::Object * > *);
public: // actually protected
  virtual jboolean validatePendingAdd(::java::lang::Object *);
  virtual jboolean validatePendingRemove(::java::lang::Object *);
public:
  virtual void vetoableChange(::java::beans::PropertyChangeEvent *);
  virtual void writeChildren(::java::io::ObjectOutputStream *);
private:
  static const jlong serialVersionUID = -4879613978649577204LL;
public: // actually protected
  ::java::util::ArrayList * __attribute__((aligned(__alignof__( ::java::beans::beancontext::BeanContextChildSupport)))) bcmListeners;
  ::java::util::HashMap * children;
  jboolean designTime;
  ::java::util::Locale * locale;
  jboolean okToUseGui__;
public:
  static ::java::lang::Class class$;
};

#endif // __java_beans_beancontext_BeanContextSupport__
