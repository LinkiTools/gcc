
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_print_attribute_HashPrintServiceAttributeSet__
#define __javax_print_attribute_HashPrintServiceAttributeSet__

#pragma interface

#include <javax/print/attribute/HashAttributeSet.h>
#include <gcj/array.h>

extern "Java"
{
  namespace javax
  {
    namespace print
    {
      namespace attribute
      {
          class HashPrintServiceAttributeSet;
          class PrintServiceAttribute;
          class PrintServiceAttributeSet;
      }
    }
  }
}

class javax::print::attribute::HashPrintServiceAttributeSet : public ::javax::print::attribute::HashAttributeSet
{

public:
  HashPrintServiceAttributeSet();
  HashPrintServiceAttributeSet(::javax::print::attribute::PrintServiceAttribute *);
  HashPrintServiceAttributeSet(JArray< ::javax::print::attribute::PrintServiceAttribute * > *);
  HashPrintServiceAttributeSet(::javax::print::attribute::PrintServiceAttributeSet *);
private:
  static const jlong serialVersionUID = 6642904616179203070LL;
public:
  static ::java::lang::Class class$;
};

#endif // __javax_print_attribute_HashPrintServiceAttributeSet__
