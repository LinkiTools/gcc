// resolve.cc - Code for linking and resolving classes and pool entries.

/* Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004 Free Software Foundation

   This file is part of libgcj.

This software is copyrighted work licensed under the terms of the
Libgcj License.  Please consult the file "LIBGCJ_LICENSE" for
details.  */

/* Author: Kresten Krab Thorup <krab@gnu.org>  */

#include <config.h>
#include <platform.h>

#include <java-interp.h>

#include <jvm.h>
#include <gcj/cni.h>
#include <string.h>
#include <java-cpool.h>
#include <execution.h>
#include <java/lang/Class.h>
#include <java/lang/String.h>
#include <java/lang/StringBuffer.h>
#include <java/lang/Thread.h>
#include <java/lang/InternalError.h>
#include <java/lang/VirtualMachineError.h>
#include <java/lang/VerifyError.h>
#include <java/lang/NoSuchFieldError.h>
#include <java/lang/NoSuchMethodError.h>
#include <java/lang/ClassFormatError.h>
#include <java/lang/IllegalAccessError.h>
#include <java/lang/AbstractMethodError.h>
#include <java/lang/NoClassDefFoundError.h>
#include <java/lang/IncompatibleClassChangeError.h>
#include <java/lang/VMClassLoader.h>
#include <java/lang/reflect/Modifier.h>

using namespace gcj;

typedef unsigned int uaddr __attribute__ ((mode (pointer)));

template<typename T>
struct aligner
{
  char c;
  T field;
};

#define ALIGNOF(TYPE) (offsetof (aligner<TYPE>, field))

// This returns the alignment of a type as it would appear in a
// structure.  This can be different from the alignment of the type
// itself.  For instance on x86 double is 8-aligned but struct{double}
// is 4-aligned.
int
_Jv_Linker::get_alignment_from_class (jclass klass)
{
  if (klass == JvPrimClass (byte))
    return ALIGNOF (jbyte);
  else if (klass == JvPrimClass (short))
    return ALIGNOF (jshort);
  else if (klass == JvPrimClass (int)) 
    return ALIGNOF (jint);
  else if (klass == JvPrimClass (long))
    return ALIGNOF (jlong);
  else if (klass == JvPrimClass (boolean))
    return ALIGNOF (jboolean);
  else if (klass == JvPrimClass (char))
    return ALIGNOF (jchar);
  else if (klass == JvPrimClass (float))
    return ALIGNOF (jfloat);
  else if (klass == JvPrimClass (double))
    return ALIGNOF (jdouble);
  else
    return ALIGNOF (jobject);
}

void
_Jv_Linker::resolve_field (_Jv_Field *field, java::lang::ClassLoader *loader)
{
  if (! field->isResolved ())
    {
      _Jv_Utf8Const *sig = (_Jv_Utf8Const*)field->type;
      field->type = _Jv_FindClassFromSignature (sig->data, loader);
      field->flags &= ~_Jv_FIELD_UNRESOLVED_FLAG;
    }
}

_Jv_word
_Jv_Linker::resolve_pool_entry (jclass klass, int index)
{
  using namespace java::lang::reflect;

  _Jv_Constants *pool = &klass->constants;

  if ((pool->tags[index] & JV_CONSTANT_ResolvedFlag) != 0)
    return pool->data[index];

  switch (pool->tags[index])
    {
    case JV_CONSTANT_Class:
      {
	_Jv_Utf8Const *name = pool->data[index].utf8;

	jclass found;
	if (name->data[0] == '[')
	  found = _Jv_FindClassFromSignature (&name->data[0],
					      klass->loader);
	else
	  found = _Jv_FindClass (name, klass->loader);

	if (! found)
	  {
	    jstring str = _Jv_NewStringUTF (name->data);
	    // This exception is specified in JLS 2nd Ed, section 5.1.
	    throw new java::lang::NoClassDefFoundError (str);
	  }

	// Check accessibility, but first strip array types as
	// _Jv_ClassNameSamePackage can't handle arrays.
	jclass check;
	for (check = found;
	     check && check->isArray();
	     check = check->getComponentType())
	  ;
	if ((found->accflags & Modifier::PUBLIC) == Modifier::PUBLIC
	    || (_Jv_ClassNameSamePackage (check->name,
					  klass->name)))
	  {
	    pool->data[index].clazz = found;
	    pool->tags[index] |= JV_CONSTANT_ResolvedFlag;
	  }
	else
	  throw new java::lang::IllegalAccessError (found->getName());
      }
      break;

    case JV_CONSTANT_String:
      {
	jstring str;
	str = _Jv_NewStringUtf8Const (pool->data[index].utf8);
	pool->data[index].o = str;
	pool->tags[index] |= JV_CONSTANT_ResolvedFlag;
      }
      break;

    case JV_CONSTANT_Fieldref:
      {
	_Jv_ushort class_index, name_and_type_index;
	_Jv_loadIndexes (&pool->data[index],
			 class_index,
			 name_and_type_index);
	jclass owner = (resolve_pool_entry (klass, class_index)).clazz;

	if (owner != klass)
	  _Jv_InitClass (owner);

	_Jv_ushort name_index, type_index;
	_Jv_loadIndexes (&pool->data[name_and_type_index],
			 name_index,
			 type_index);

	_Jv_Utf8Const *field_name = pool->data[name_index].utf8;
	_Jv_Utf8Const *field_type_name = pool->data[type_index].utf8;

	// FIXME: The implementation of this function
	// (_Jv_FindClassFromSignature) will generate an instance of
	// _Jv_Utf8Const for each call if the field type is a class name
	// (Lxx.yy.Z;).  This may be too expensive to do for each and
	// every fieldref being resolved.  For now, we fix the problem by
	// only doing it when we have a loader different from the class
	// declaring the field.

	jclass field_type = 0;

	if (owner->loader != klass->loader)
	  field_type = _Jv_FindClassFromSignature (field_type_name->data,
						   klass->loader);
      
	_Jv_Field* the_field = 0;

	for (jclass cls = owner; cls != 0; cls = cls->getSuperclass ())
	  {
	    for (int i = 0;  i < cls->field_count;  i++)
	      {
		_Jv_Field *field = &cls->fields[i];
		if (! _Jv_equalUtf8Consts (field->name, field_name))
		  continue;

		if (_Jv_CheckAccess (klass, cls, field->flags))
		  {
		    // Resolve the field using the class' own loader if
		    // necessary.

		    if (!field->isResolved ())
		      resolve_field (field, cls->loader);

		    if (field_type != 0 && field->type != field_type)
		      throw new java::lang::LinkageError
			(JvNewStringLatin1 
			 ("field type mismatch with different loaders"));

		    the_field = field;
		    goto end_of_field_search;
		  }
		else
		  {
		    java::lang::StringBuffer *sb
		      = new java::lang::StringBuffer ();
		    sb->append(klass->getName());
		    sb->append(JvNewStringLatin1(": "));
		    sb->append(cls->getName());
		    sb->append(JvNewStringLatin1("."));
		    sb->append(_Jv_NewStringUtf8Const (field_name));
		    throw new java::lang::IllegalAccessError(sb->toString());
		  }
	      }
	  }

      end_of_field_search:
	if (the_field == 0)
	  {
	    java::lang::StringBuffer *sb = new java::lang::StringBuffer();
	    sb->append(JvNewStringLatin1("field "));
	    sb->append(owner->getName());
	    sb->append(JvNewStringLatin1("."));
	    sb->append(_Jv_NewStringUTF(field_name->data));
	    sb->append(JvNewStringLatin1(" was not found."));
	    throw
	      new java::lang::IncompatibleClassChangeError (sb->toString());
	  }

	pool->data[index].field = the_field;
	pool->tags[index] |= JV_CONSTANT_ResolvedFlag;
      }
      break;

    case JV_CONSTANT_Methodref:
    case JV_CONSTANT_InterfaceMethodref:
      {
	_Jv_ushort class_index, name_and_type_index;
	_Jv_loadIndexes (&pool->data[index],
			 class_index,
			 name_and_type_index);
	jclass owner = (resolve_pool_entry (klass, class_index)).clazz;

	if (owner != klass)
	  _Jv_InitClass (owner);

	_Jv_ushort name_index, type_index;
	_Jv_loadIndexes (&pool->data[name_and_type_index],
			 name_index,
			 type_index);

	_Jv_Utf8Const *method_name = pool->data[name_index].utf8;
	_Jv_Utf8Const *method_signature = pool->data[type_index].utf8;

	_Jv_Method *the_method = 0;
	jclass found_class = 0;

	// First search the class itself.
	the_method = _Jv_SearchMethodInClass (owner, klass, 
					      method_name, method_signature);

	if (the_method != 0)
	  {
	    found_class = owner;
	    goto end_of_method_search;
	  }

	// If we are resolving an interface method, search the
	// interface's superinterfaces (A superinterface is not an
	// interface's superclass - a superinterface is implemented by
	// the interface).
	if (pool->tags[index] == JV_CONSTANT_InterfaceMethodref)
	  {
	    _Jv_ifaces ifaces;
	    ifaces.count = 0;
	    ifaces.len = 4;
	    ifaces.list = (jclass *) _Jv_Malloc (ifaces.len
						 * sizeof (jclass *));

	    get_interfaces (owner, &ifaces);	  

	    for (int i = 0; i < ifaces.count; i++)
	      {
		jclass cls = ifaces.list[i];
		the_method = _Jv_SearchMethodInClass (cls, klass, method_name, 
						      method_signature);
		if (the_method != 0)
		  {
		    found_class = cls;
		    break;
		  }
	      }

	    _Jv_Free (ifaces.list);

	    if (the_method != 0)
	      goto end_of_method_search;
	  }

	// Finally, search superclasses. 
	for (jclass cls = owner->getSuperclass (); cls != 0; 
	     cls = cls->getSuperclass ())
	  {
	    the_method = _Jv_SearchMethodInClass (cls, klass, method_name,
						  method_signature);
	    if (the_method != 0)
	      {
		found_class = cls;
		break;
	      }
	  }

      end_of_method_search:
    
	// FIXME: if (cls->loader != klass->loader), then we
	// must actually check that the types of arguments
	// correspond.  That is, for each argument type, and
	// the return type, doing _Jv_FindClassFromSignature
	// with either loader should produce the same result,
	// i.e., exactly the same jclass object. JVMS 5.4.3.3    
    
	if (the_method == 0)
	  {
	    java::lang::StringBuffer *sb = new java::lang::StringBuffer();
	    sb->append(JvNewStringLatin1("method "));
	    sb->append(owner->getName());
	    sb->append(JvNewStringLatin1("."));
	    sb->append(_Jv_NewStringUTF(method_name->data));
	    sb->append(JvNewStringLatin1(" with signature "));
	    sb->append(_Jv_NewStringUTF(method_signature->data));
	    sb->append(JvNewStringLatin1(" was not found."));
	    throw new java::lang::NoSuchMethodError (sb->toString());
	  }
      
	int vtable_index = -1;
	if (pool->tags[index] != JV_CONSTANT_InterfaceMethodref)
	  vtable_index = (jshort)the_method->index;

	pool->data[index].rmethod
	  = klass->engine->resolve_method(the_method,
					  found_class,
					  ((the_method->accflags
					    & Modifier::STATIC) != 0),
					  vtable_index);
	pool->tags[index] |= JV_CONSTANT_ResolvedFlag;
      }
      break;
    }
  return pool->data[index];
}

// This function is used to lazily locate superclasses and
// superinterfaces.  This must be called with the class lock held.
void
_Jv_Linker::resolve_class_ref (jclass klass, jclass *classref)
{
  jclass ret = *classref;

  // If superclass looks like a constant pool entry, resolve it now.
  if (ret && (uaddr) ret < (uaddr) klass->constants.size)
    {
      if (klass->state < JV_STATE_LINKED)
	{
	  _Jv_Utf8Const *name = klass->constants.data[(uaddr) *classref].utf8;
	  ret = _Jv_FindClass (name, klass->loader);
	  if (! ret)
	    {
	      jstring str = _Jv_NewStringUTF (name->data);
	      throw new java::lang::NoClassDefFoundError (str);
	    }
	}
      else
	ret = klass->constants.data[(uaddr) classref].clazz;
      *classref = ret;
    }
}

// Find a method declared in the cls that is referenced from klass and
// perform access checks.
_Jv_Method *
_Jv_SearchMethodInClass (jclass cls, jclass klass, 
                         _Jv_Utf8Const *method_name, 
			 _Jv_Utf8Const *method_signature)
{
  using namespace java::lang::reflect;

  for (int i = 0;  i < cls->method_count;  i++)
    {
      _Jv_Method *method = &cls->methods[i];
      if (   (!_Jv_equalUtf8Consts (method->name,
				    method_name))
	  || (!_Jv_equalUtf8Consts (method->signature,
				    method_signature)))
	continue;

      if (_Jv_CheckAccess (klass, cls, method->accflags))
	return method;
      else
	{
	  java::lang::StringBuffer *sb = new java::lang::StringBuffer();
	  sb->append(klass->getName());
	  sb->append(JvNewStringLatin1(": "));
	  sb->append(cls->getName());
	  sb->append(JvNewStringLatin1("."));
	  sb->append(_Jv_NewStringUTF(method_name->data));
	  sb->append(_Jv_NewStringUTF(method_signature->data));
	  throw new java::lang::IllegalAccessError (sb->toString());
	}
    }
  return 0;
}


#define INITIAL_IOFFSETS_LEN 4
#define INITIAL_IFACES_LEN 4

static _Jv_IDispatchTable null_idt = { {SHRT_MAX, 0, NULL} };

// Generate tables for constant-time assignment testing and interface
// method lookup. This implements the technique described by Per Bothner
// <per@bothner.com> on the java-discuss mailing list on 1999-09-02:
// http://gcc.gnu.org/ml/java/1999-q3/msg00377.html
void
_Jv_Linker::prepare_constant_time_tables (jclass klass)
{  
  if (klass->isPrimitive () || klass->isInterface ())
    return;

  // Short-circuit in case we've been called already.
  if ((klass->idt != NULL) || klass->depth != 0)
    return;

  // Calculate the class depth and ancestor table. The depth of a class 
  // is how many "extends" it is removed from Object. Thus the depth of 
  // java.lang.Object is 0, but the depth of java.io.FilterOutputStream 
  // is 2. Depth is defined for all regular and array classes, but not 
  // interfaces or primitive types.
   
  jclass klass0 = klass;
  jboolean has_interfaces = 0;
  while (klass0 != &java::lang::Object::class$)
    {
      has_interfaces += klass0->interface_count;
      klass0 = klass0->superclass;
      klass->depth++;
    }

  // We do class member testing in constant time by using a small table 
  // of all the ancestor classes within each class. The first element is 
  // a pointer to the current class, and the rest are pointers to the 
  // classes ancestors, ordered from the current class down by decreasing 
  // depth. We do not include java.lang.Object in the table of ancestors, 
  // since it is redundant.

  // FIXME: _Jv_AllocBytes
  klass->ancestors = (jclass *) _Jv_Malloc (klass->depth
						* sizeof (jclass));
  klass0 = klass;
  for (int index = 0; index < klass->depth; index++)
    {
      klass->ancestors[index] = klass0;
      klass0 = klass0->superclass;
    }

  if ((klass->accflags & java::lang::reflect::Modifier::ABSTRACT) != 0)
    return;

  // Optimization: If class implements no interfaces, use a common
  // predefined interface table.
  if (!has_interfaces)
    {
      klass->idt = &null_idt;
      return;
    }

  // FIXME: _Jv_AllocBytes
  klass->idt = 
    (_Jv_IDispatchTable *) _Jv_Malloc (sizeof (_Jv_IDispatchTable));

  _Jv_ifaces ifaces;
  ifaces.count = 0;
  ifaces.len = INITIAL_IFACES_LEN;
  ifaces.list = (jclass *) _Jv_Malloc (ifaces.len * sizeof (jclass *));

  int itable_size = get_interfaces (klass, &ifaces);

  if (ifaces.count > 0)
    {
      klass->idt->cls.itable = 
	// FIXME: _Jv_AllocBytes
	(void **) _Jv_Malloc (itable_size * sizeof (void *));
      klass->idt->cls.itable_length = itable_size;

      jshort *itable_offsets = 
	(jshort *) _Jv_Malloc (ifaces.count * sizeof (jshort));

      generate_itable (klass, &ifaces, itable_offsets);

      jshort cls_iindex = find_iindex (ifaces.list, itable_offsets,
				       ifaces.count);

      for (int i = 0; i < ifaces.count; i++)
	{
	  ifaces.list[i]->idt->iface.ioffsets[cls_iindex] =
	    itable_offsets[i];
	}

      klass->idt->cls.iindex = cls_iindex;	    

      _Jv_Free (ifaces.list);
      _Jv_Free (itable_offsets);
    }
  else 
    {
      klass->idt->cls.iindex = SHRT_MAX;
    }
}

// Return index of item in list, or -1 if item is not present.
inline jshort
_Jv_Linker::indexof (void *item, void **list, jshort list_len)
{
  for (int i=0; i < list_len; i++)
    {
      if (list[i] == item)
        return i;
    }
  return -1;
}

// Find all unique interfaces directly or indirectly implemented by klass.
// Returns the size of the interface dispatch table (itable) for klass, which 
// is the number of unique interfaces plus the total number of methods that 
// those interfaces declare. May extend ifaces if required.
jshort
_Jv_Linker::get_interfaces (jclass klass, _Jv_ifaces *ifaces)
{
  jshort result = 0;
  
  for (int i = 0; i < klass->interface_count; i++)
    {
      jclass iface = klass->interfaces[i];

      /* Make sure interface is linked.  */
      wait_for_state(iface, JV_STATE_LINKED);

      if (indexof (iface, (void **) ifaces->list, ifaces->count) == -1)
        {
	  if (ifaces->count + 1 >= ifaces->len)
	    {
	      /* Resize ifaces list */
	      ifaces->len = ifaces->len * 2;
	      ifaces->list
		= (jclass *) _Jv_Realloc (ifaces->list,
					  ifaces->len * sizeof(jclass));
	    }
	  ifaces->list[ifaces->count] = iface;
	  ifaces->count++;

	  result += get_interfaces (klass->interfaces[i], ifaces);
	}
    }
    
  if (klass->isInterface())
    result += klass->method_count + 1;
  else if (klass->superclass)
    result += get_interfaces (klass->superclass, ifaces);
  return result;
}

// Fill out itable in klass, resolving method declarations in each ifaces.
// itable_offsets is filled out with the position of each iface in itable,
// such that itable[itable_offsets[n]] == ifaces.list[n].
void
_Jv_Linker::generate_itable (jclass klass, _Jv_ifaces *ifaces,
			       jshort *itable_offsets)
{
  void **itable = klass->idt->cls.itable;
  jshort itable_pos = 0;

  for (int i = 0; i < ifaces->count; i++)
    { 
      jclass iface = ifaces->list[i];
      itable_offsets[i] = itable_pos;
      itable_pos = append_partial_itable (klass, iface, itable, itable_pos);

      /* Create interface dispatch table for iface */
      if (iface->idt == NULL)
	{
	  // FIXME: _Jv_AllocBytes
	  iface->idt
	    = (_Jv_IDispatchTable *) _Jv_Malloc (sizeof (_Jv_IDispatchTable));

	  // The first element of ioffsets is its length (itself included).
	  // FIXME: _Jv_AllocBytes
	  jshort *ioffsets = (jshort *) _Jv_Malloc (INITIAL_IOFFSETS_LEN
						    * sizeof (jshort));
	  ioffsets[0] = INITIAL_IOFFSETS_LEN;
	  for (int i = 1; i < INITIAL_IOFFSETS_LEN; i++)
	    ioffsets[i] = -1;

	  iface->idt->iface.ioffsets = ioffsets;	    
	}
    }
}

// Format method name for use in error messages.
jstring
_Jv_GetMethodString (jclass klass, _Jv_Utf8Const *name)
{
  jstring r = JvNewStringUTF (klass->name->data);
  r = r->concat (JvNewStringUTF ("."));
  r = r->concat (JvNewStringUTF (name->data));
  return r;
}

void 
_Jv_ThrowNoSuchMethodError ()
{
  throw new java::lang::NoSuchMethodError;
}

// Each superinterface of a class (i.e. each interface that the class
// directly or indirectly implements) has a corresponding "Partial
// Interface Dispatch Table" whose size is (number of methods + 1) words.
// The first word is a pointer to the interface (i.e. the java.lang.Class
// instance for that interface).  The remaining words are pointers to the
// actual methods that implement the methods declared in the interface,
// in order of declaration.
//
// Append partial interface dispatch table for "iface" to "itable", at
// position itable_pos.
// Returns the offset at which the next partial ITable should be appended.
jshort
_Jv_Linker::append_partial_itable (jclass klass, jclass iface,
				     void **itable, jshort pos)
{
  using namespace java::lang::reflect;

  itable[pos++] = (void *) iface;
  _Jv_Method *meth;
  
  for (int j=0; j < iface->method_count; j++)
    {
      meth = NULL;
      for (jclass cl = klass; cl; cl = cl->getSuperclass())
        {
	  meth = _Jv_GetMethodLocal (cl, iface->methods[j].name,
				     iface->methods[j].signature);
		 
	  if (meth)
	    break;
	}

      if (meth && (meth->name->data[0] == '<'))
	{
	  // leave a placeholder in the itable for hidden init methods.
          itable[pos] = NULL;	
	}
      else if (meth)
        {
	  if ((meth->accflags & Modifier::STATIC) != 0)
	    throw new java::lang::IncompatibleClassChangeError
	      (_Jv_GetMethodString (klass, meth->name));
	  if ((meth->accflags & Modifier::ABSTRACT) != 0)
	    throw new java::lang::AbstractMethodError
	      (_Jv_GetMethodString (klass, meth->name));
	  if ((meth->accflags & Modifier::PUBLIC) == 0)
	    throw new java::lang::IllegalAccessError
	      (_Jv_GetMethodString (klass, meth->name));

	  itable[pos] = meth->ncode;
	}
      else
        {
	  // The method doesn't exist in klass. Binary compatibility rules
	  // permit this, so we delay the error until runtime using a pointer
	  // to a method which throws an exception.
	  itable[pos] = (void *) _Jv_ThrowNoSuchMethodError;
	}
      pos++;
    }
    
  return pos;
}

static _Jv_Mutex_t iindex_mutex;
static bool iindex_mutex_initialized = false;

// We need to find the correct offset in the Class Interface Dispatch 
// Table for a given interface. Once we have that, invoking an interface 
// method just requires combining the Method's index in the interface 
// (known at compile time) to get the correct method.  Doing a type test 
// (cast or instanceof) is the same problem: Once we have a possible Partial 
// Interface Dispatch Table, we just compare the first element to see if it 
// matches the desired interface. So how can we find the correct offset?  
// Our solution is to keep a vector of candiate offsets in each interface 
// (idt->iface.ioffsets), and in each class we have an index 
// (idt->cls.iindex) used to select the correct offset from ioffsets.
//
// Calculate and return iindex for a new class. 
// ifaces is a vector of num interfaces that the class implements.
// offsets[j] is the offset in the interface dispatch table for the
// interface corresponding to ifaces[j].
// May extend the interface ioffsets if required.
jshort
_Jv_Linker::find_iindex (jclass *ifaces, jshort *offsets, jshort num)
{
  int i;
  int j;
  
  // Acquire a global lock to prevent itable corruption in case of multiple 
  // classes that implement an intersecting set of interfaces being linked
  // simultaneously. We can assume that the mutex will be initialized
  // single-threaded.
  if (! iindex_mutex_initialized)
    {
      _Jv_MutexInit (&iindex_mutex);
      iindex_mutex_initialized = true;
    }
  
  _Jv_MutexLock (&iindex_mutex);
  
  for (i=1;; i++)  /* each potential position in ioffsets */
    {
      for (j=0;; j++)  /* each iface */
        {
	  if (j >= num)
	    goto found;
	  if (i >= ifaces[j]->idt->iface.ioffsets[0])
	    continue;
	  int ioffset = ifaces[j]->idt->iface.ioffsets[i];
	  /* We can potentially share this position with another class. */
	  if (ioffset >= 0 && ioffset != offsets[j])
	    break; /* Nope. Try next i. */	  
	}
    }
  found:
  for (j = 0; j < num; j++)
    {
      int len = ifaces[j]->idt->iface.ioffsets[0];
      if (i >= len) 
	{
	  /* Resize ioffsets. */
	  int newlen = 2 * len;
	  if (i >= newlen)
	    newlen = i + 3;
	  jshort *old_ioffsets = ifaces[j]->idt->iface.ioffsets;
	  // FIXME: _Jv_AllocBytes
	  jshort *new_ioffsets = (jshort *) _Jv_Malloc (newlen
							* sizeof(jshort));
	  memcpy (&new_ioffsets[1], &old_ioffsets[1], len * sizeof (jshort));
	  new_ioffsets[0] = newlen;

	  while (len < newlen)
	    new_ioffsets[len++] = -1;
	  
	  ifaces[j]->idt->iface.ioffsets = new_ioffsets;
	}
      ifaces[j]->idt->iface.ioffsets[i] = offsets[j];
    }

  _Jv_MutexUnlock (&iindex_mutex);

  return i;
}


// Functions for indirect dispatch (symbolic virtual binding) support.

// There are three tables, atable otable and itable.  atable is an
// array of addresses, and otable is an array of offsets, and these
// are used for static and virtual members respectively.  itable is an
// array of pairs {address, index} where each address is a pointer to
// an interface.

// {a,o,i}table_syms is an array of _Jv_MethodSymbols.  Each such
// symbol is a tuple of {classname, member name, signature}.

// Set this to true to enable debugging of indirect dispatch tables/linking.
static bool debug_link = false;

// _Jv_LinkSymbolTable() scans these two arrays and fills in the
// corresponding atable and otable with the addresses of static
// members and the offsets of virtual members.

// The offset (in bytes) for each resolved method or field is placed
// at the corresponding position in the virtual method offset table
// (klass->otable). 

// The same otable and atable may be shared by many classes.

// This must be called while holding the class lock.

void
_Jv_Linker::link_symbol_table (jclass klass)
{
  int index = 0;
  _Jv_MethodSymbol sym;
  if (klass->otable == NULL
      || klass->otable->state != 0)
    goto atable;
   
  klass->otable->state = 1;

  if (debug_link)
    fprintf (stderr, "Fixing up otable in %s:\n", klass->name->data);
  for (index = 0;
       (sym = klass->otable_syms[index]).class_name != NULL;
       ++index)
    {
      jclass target_class = _Jv_FindClass (sym.class_name, klass->loader);
      _Jv_Method *meth = NULL;            

      const _Jv_Utf8Const *signature = sym.signature;

      {
	static char *bounce = (char *)_Jv_ThrowNoSuchMethodError;
	ptrdiff_t offset = (char *)(klass->vtable) - bounce;
	klass->otable->offsets[index] = offset;
      }

      if (target_class == NULL)
	throw new java::lang::NoClassDefFoundError 
	  (_Jv_NewStringUTF (sym.class_name->data));

      // We're looking for a field or a method, and we can tell
      // which is needed by looking at the signature.
      if (signature->length >= 2
	  && signature->data[0] == '(')
	{
	  // Looks like someone is trying to invoke an interface method
	  if (target_class->isInterface())
	    {
	      using namespace java::lang;
	      StringBuffer *sb = new StringBuffer();
	      sb->append(JvNewStringLatin1("found interface "));
	      sb->append(target_class->getName());
	      sb->append(JvNewStringLatin1(" when searching for a class"));
	      throw new VerifyError(sb->toString());
	    }

 	  // If the target class does not have a vtable_method_count yet, 
	  // then we can't tell the offsets for its methods, so we must lay 
	  // it out now.
	  wait_for_state(target_class, JV_STATE_PREPARED);

	  meth = _Jv_LookupDeclaredMethod(target_class, sym.name, 
					  sym.signature);

	  if (meth != NULL)
	    {
	      int offset = _Jv_VTable::idx_to_offset (meth->index);
	      if (offset == -1)
		JvFail ("Bad method index");
	      JvAssert (meth->index < target_class->vtable_method_count);
	      klass->otable->offsets[index] = offset;
	    }
	  if (debug_link)
	    fprintf (stderr, "  offsets[%d] = %d (class %s@%p : %s(%s))\n",
		     index,
		     klass->otable->offsets[index],
		     (const char*)target_class->name->data,
		     target_class,
		     (const char*)sym.name->data,
		     (const char*)signature->data);
	  continue;
	}

      // try fields
      {
	_Jv_Field *the_field = NULL;

	for (jclass cls = target_class; cls != 0; cls = cls->getSuperclass ())
	  {
	    for (int i = 0; i < cls->field_count; i++)
	      {
		_Jv_Field *field = &cls->fields[i];
		if (! _Jv_equalUtf8Consts (field->name, sym.name))
		  continue;

		// FIXME: What access checks should we perform here?
// 		if (_Jv_CheckAccess (klass, cls, field->flags))
// 		  {

		wait_for_state(cls, JV_STATE_PREPARED);

		if (!field->isResolved ())
		  resolve_field (field, cls->loader);

// 		if (field_type != 0 && field->type != field_type)
// 		  throw new java::lang::LinkageError
// 		    (JvNewStringLatin1 
// 		     ("field type mismatch with different loaders"));

		the_field = field;
		if (debug_link)
		  fprintf (stderr, "  offsets[%d] = %d (class %s@%p : %s)\n",
			   index,
			   field->u.boffset,
			   (const char*)cls->name->data,
			   cls,
			   (const char*)field->name->data);
		goto end_of_field_search;
	      }
	  }
      end_of_field_search:
	if (the_field != NULL)
	  {
	    if ((the_field->flags & java::lang::reflect::Modifier::STATIC))
	      throw new java::lang::IncompatibleClassChangeError;
	    else
	      klass->otable->offsets[index] = the_field->u.boffset;
	  }
	else
	  {
	    throw new java::lang::NoSuchFieldError
	      (_Jv_NewStringUtf8Const (sym.name));
	  }
      }
    }

 atable:
  if (klass->atable == NULL || klass->atable->state != 0)
    goto itable;

  klass->atable->state = 1;

  for (index = 0;
       (sym = klass->atable_syms[index]).class_name != NULL;
       ++index)
    {
      jclass target_class = _Jv_FindClass (sym.class_name, klass->loader);
      _Jv_Method *meth = NULL;            
      const _Jv_Utf8Const *signature = sym.signature;

      // ??? Setting this pointer to null will at least get us a
      // NullPointerException
      klass->atable->addresses[index] = NULL;
      
      if (target_class == NULL)
	throw new java::lang::NoClassDefFoundError 
	  (_Jv_NewStringUTF (sym.class_name->data));
      
      // We're looking for a static field or a static method, and we
      // can tell which is needed by looking at the signature.
      if (signature->length >= 2
	  && signature->data[0] == '(')
	{
 	  // If the target class does not have a vtable_method_count yet, 
	  // then we can't tell the offsets for its methods, so we must lay 
	  // it out now.
	  wait_for_state (target_class, JV_STATE_PREPARED);

	  // Interface methods cannot have bodies.
	  if (target_class->isInterface())
	    {
	      using namespace java::lang;
	      StringBuffer *sb = new StringBuffer();
	      sb->append(JvNewStringLatin1("class "));
	      sb->append(target_class->getName());
	      sb->append(JvNewStringLatin1(" is an interface: "
					   "class expected"));
	      throw new VerifyError(sb->toString());
	    }

	  meth = _Jv_LookupDeclaredMethod(target_class, sym.name, 
					  sym.signature);

	  if (meth != NULL)
	    {
	      if (meth->ncode) // Maybe abstract?
		{
		  klass->atable->addresses[index] = meth->ncode;
		  if (debug_link)
		    fprintf (stderr, "  addresses[%d] = %p (class %s@%p : %s(%s))\n",
			     index,
			     &klass->atable->addresses[index],
			     (const char*)target_class->name->data,
			     klass,
			     (const char*)sym.name->data,
			     (const char*)signature->data);
		}
	    }
	  else
	    klass->atable->addresses[index]
	      = (void *)_Jv_ThrowNoSuchMethodError;

	  continue;
	}

      // try fields
      {
	_Jv_Field *the_field = NULL;

	for (jclass cls = target_class; cls != 0; cls = cls->getSuperclass ())
	  {
	    for (int i = 0; i < cls->field_count; i++)
	      {
		_Jv_Field *field = &cls->fields[i];
		if (! _Jv_equalUtf8Consts (field->name, sym.name))
		  continue;

		// FIXME: What access checks should we perform here?
// 		if (_Jv_CheckAccess (klass, cls, field->flags))
// 		  {

		if (!field->isResolved ())
		  resolve_field (field, cls->loader);

		wait_for_state(target_class, JV_STATE_PREPARED);

// 		if (field_type != 0 && field->type != field_type)
// 		  throw new java::lang::LinkageError
// 		    (JvNewStringLatin1 
// 		     ("field type mismatch with different loaders"));

		the_field = field;
		goto end_of_static_field_search;
	      }
	  }
      end_of_static_field_search:
	if (the_field != NULL)
	  {
	    if ((the_field->flags & java::lang::reflect::Modifier::STATIC))
	      klass->atable->addresses[index] = the_field->u.addr;
	    else
	      throw new java::lang::IncompatibleClassChangeError;
	  }
	else
	  {
	    throw new java::lang::NoSuchFieldError
	      (_Jv_NewStringUtf8Const (sym.name));
	  }
      }
    }

 itable:
  if (klass->itable == NULL
      || klass->itable->state != 0)
    return;

  klass->itable->state = 1;

  for (index = 0;
       (sym = klass->itable_syms[index]).class_name != NULL; 
       ++index)
    {
      jclass target_class = _Jv_FindClass (sym.class_name, klass->loader);
      const _Jv_Utf8Const *signature = sym.signature;

      jclass cls;
      int i;

      bool found = _Jv_getInterfaceMethod (target_class, cls, i,
					   sym.name, sym.signature);

      if (found)
	{
	  klass->itable->addresses[index * 2] = cls;
	  klass->itable->addresses[index * 2 + 1] = (void *) i;
	  if (debug_link)
	    {
	      fprintf (stderr, "  interfaces[%d] = %p (interface %s@%p : %s(%s))\n",
		       index,
		       klass->itable->addresses[index * 2],
		       (const char*)cls->name->data,
		       cls,
		       (const char*)sym.name->data,
		       (const char*)signature->data);
	      fprintf (stderr, "            [%d] = offset %d\n",
		       index + 1,
		       (int)klass->itable->addresses[index * 2 + 1]);
	    }

	}
      else
	throw new java::lang::IncompatibleClassChangeError;
    }

}

// For each catch_record in the list of caught classes, fill in the
// address field.
void 
_Jv_Linker::link_exception_table (jclass self)
{
  struct _Jv_CatchClass *catch_record = self->catch_classes;
  if (!catch_record || catch_record->classname)
    return;  
  catch_record++;
  while (catch_record->classname)
    {
      try
	{
	  jclass target_class
	    = _Jv_FindClass (catch_record->classname,  
			     self->getClassLoaderInternal ());
	  *catch_record->address = target_class;
	}
      catch (::java::lang::Throwable *t)
	{
	  // FIXME: We need to do something better here.
	  *catch_record->address = 0;
	}
      catch_record++;
    }
  self->catch_classes->classname = (_Jv_Utf8Const *)-1;
}
  
// This is put in empty vtable slots.
static void
_Jv_abstractMethodError (void)
{
  throw new java::lang::AbstractMethodError();
}

// Set itable method indexes for members of interface IFACE.
void
_Jv_Linker::layout_interface_methods (jclass iface)
{
  if (! iface->isInterface())
    return;

  // itable indexes start at 1. 
  // FIXME: Static initalizers currently get a NULL placeholder entry in the
  // itable so they are also assigned an index here.
  for (int i = 0; i < iface->method_count; i++)
    iface->methods[i].index = i + 1;
}

// Prepare virtual method declarations in KLASS, and any superclasses
// as required, by determining their vtable index, setting
// method->index, and finally setting the class's vtable_method_count.
// Must be called with the lock for KLASS held.
void
_Jv_Linker::layout_vtable_methods (jclass klass)
{
  if (klass->vtable != NULL || klass->isInterface() 
      || klass->vtable_method_count != -1)
    return;

  jclass superclass = klass->getSuperclass();

  if (superclass != NULL && superclass->vtable_method_count == -1)
    {
      JvSynchronize sync (superclass);
      layout_vtable_methods (superclass);
    }

  int index = (superclass == NULL ? 0 : superclass->vtable_method_count);

  for (int i = 0; i < klass->method_count; ++i)
    {
      _Jv_Method *meth = &klass->methods[i];
      _Jv_Method *super_meth = NULL;

      if (! _Jv_isVirtualMethod (meth))
	continue;

      if (superclass != NULL)
	{
	  jclass declarer;
	  super_meth = _Jv_LookupDeclaredMethod (superclass, meth->name,
						 meth->signature, &declarer);
	  // See if this method actually overrides the other method
	  // we've found.
	  if (super_meth)
	    {
	      if (! _Jv_isVirtualMethod (super_meth)
		  || ! _Jv_CheckAccess (klass, declarer,
					super_meth->accflags))
		super_meth = NULL;
	      else if ((super_meth->accflags
			& java::lang::reflect::Modifier::FINAL) != 0)
		{
		  using namespace java::lang;
		  StringBuffer *sb = new StringBuffer();
		  sb->append(JvNewStringLatin1("method "));
		  sb->append(_Jv_GetMethodString(klass, meth->name));
		  sb->append(JvNewStringLatin1(" overrides final method "));
		  sb->append(_Jv_GetMethodString(declarer, super_meth->name));
		  throw new VerifyError(sb->toString());
		}
	    }
	}

      if (super_meth)
        meth->index = super_meth->index;
      else
	meth->index = index++;
    }

  klass->vtable_method_count = index;
}

// Set entries in VTABLE for virtual methods declared in KLASS. If
// KLASS has an immediate abstract parent, recursively do its methods
// first.  FLAGS is used to determine which slots we've actually set.
void
_Jv_Linker::set_vtable_entries (jclass klass, _Jv_VTable *vtable,
				  jboolean *flags)
{
  using namespace java::lang::reflect;

  jclass superclass = klass->getSuperclass();

  if (superclass != NULL && (superclass->getModifiers() & Modifier::ABSTRACT))
    set_vtable_entries (superclass, vtable, flags);

  for (int i = klass->method_count - 1; i >= 0; i--)
    {
      _Jv_Method *meth = &klass->methods[i];
      if (meth->index == (_Jv_ushort) -1)
	continue;
      if ((meth->accflags & Modifier::ABSTRACT))
	{
	  vtable->set_method(meth->index, (void *) &_Jv_abstractMethodError);
	  flags[meth->index] = false;
	}
      else
	{
	  vtable->set_method(meth->index, meth->ncode);
	  flags[meth->index] = true;
	}
    }
}

// Allocate and lay out the virtual method table for KLASS.  This will
// also cause vtables to be generated for any non-abstract
// superclasses, and virtual method layout to occur for any abstract
// superclasses.  Must be called with monitor lock for KLASS held.
void
_Jv_Linker::make_vtable (jclass klass)
{
  using namespace java::lang::reflect;  

  // If the vtable exists, or for interface classes, do nothing.  All
  // other classes, including abstract classes, need a vtable.
  if (klass->vtable != NULL || klass->isInterface())
    return;

  // Ensure all the `ncode' entries are set.
  klass->engine->create_ncode(klass);

  // Class must be laid out before we can create a vtable. 
  if (klass->vtable_method_count == -1)
    layout_vtable_methods (klass);

  // Allocate the new vtable.
  _Jv_VTable *vtable = _Jv_VTable::new_vtable (klass->vtable_method_count);
  klass->vtable = vtable;

  jboolean flags[klass->vtable_method_count];
  for (int i = 0; i < klass->vtable_method_count; ++i)
    flags[i] = false;

  // Copy the vtable of the closest superclass.
  jclass superclass = klass->superclass;
  {
    JvSynchronize sync (superclass);
    make_vtable (superclass);
  }
  for (int i = 0; i < superclass->vtable_method_count; ++i)
    {
      vtable->set_method (i, superclass->vtable->get_method (i));
      flags[i] = true;
    }

  // Set the class pointer and GC descriptor.
  vtable->clas = klass;
  vtable->gc_descr = _Jv_BuildGCDescr (klass);

  // For each virtual declared in klass and any immediate abstract 
  // superclasses, set new vtable entry or override an old one.
  set_vtable_entries (klass, vtable, flags);

  // It is an error to have an abstract method in a concrete class.
  if (! (klass->accflags & Modifier::ABSTRACT))
    {
      for (int i = 0; i < klass->vtable_method_count; ++i)
	if (! flags[i])
	  {
	    using namespace java::lang;
	    while (klass != NULL)
	      {
		for (int j = 0; j < klass->method_count; ++j)
		  {
		    if (klass->methods[i].index == i)
		      {
			StringBuffer *buf = new StringBuffer ();
			buf->append (_Jv_NewStringUtf8Const (klass->methods[i].name));
			buf->append ((jchar) ' ');
			buf->append (_Jv_NewStringUtf8Const (klass->methods[i].signature));
			throw new AbstractMethodError (buf->toString ());
		      }
		  }
		klass = klass->getSuperclass ();
	      }
	    // Couldn't find the name, which is weird.
	    // But we still must throw the error.
	    throw new AbstractMethodError ();
	  }
    }
}

// Lay out the class, allocating space for static fields and computing
// offsets of instance fields.  The class lock must be held by the
// caller.
void
_Jv_Linker::ensure_fields_laid_out (jclass klass)
{  
  if (klass->size_in_bytes != -1)
    return;

  // Compute the alignment for this type by searching through the
  // superclasses and finding the maximum required alignment.  We
  // could consider caching this in the Class.
  int max_align = __alignof__ (java::lang::Object);
  jclass super = klass->getSuperclass();
  while (super != NULL)
    {
      // Ensure that our super has its super installed before
      // recursing.
      wait_for_state(super, JV_STATE_LOADING);
      ensure_fields_laid_out(super);
      int num = JvNumInstanceFields (super);
      _Jv_Field *field = JvGetFirstInstanceField (super);
      while (num > 0)
	{
	  int field_align = get_alignment_from_class (field->type);
	  if (field_align > max_align)
	    max_align = field_align;
	  ++field;
	  --num;
	}
      super = super->getSuperclass();
    }

  int instance_size;
  int static_size = 0;

  // Although java.lang.Object is never interpreted, an interface can
  // have a null superclass.  Note that we have to lay out an
  // interface because it might have static fields.
  if (klass->superclass)
    instance_size = klass->superclass->size();
  else
    instance_size = java::lang::Object::class$.size();

  for (int i = 0; i < klass->field_count; i++)
    {
      int field_size;
      int field_align;

      _Jv_Field *field = &klass->fields[i];

      if (! field->isRef ())
	{
	  // It is safe to resolve the field here, since it's a
	  // primitive class, which does not cause loading to happen.
	  resolve_field (field, klass->loader);

	  field_size = field->type->size ();
	  field_align = get_alignment_from_class (field->type);
	}
      else 
	{
	  field_size = sizeof (jobject);
	  field_align = __alignof__ (jobject);
	}

      field->bsize = field_size;

      if ((field->flags & java::lang::reflect::Modifier::STATIC))
	{
	  if (field->u.addr == NULL)
	    {
	      // This computes an offset into a region we'll allocate
	      // shortly, and then add this offset to the start
	      // address.
	      static_size       = ROUND (static_size, field_align);
	      field->u.boffset   = static_size;
	      static_size       += field_size;
	    }
	}
      else
	{
	  instance_size      = ROUND (instance_size, field_align);
	  field->u.boffset   = instance_size;
	  instance_size     += field_size;
	  if (field_align > max_align)
	    max_align = field_align;
	}
    }

  if (static_size != 0)
    klass->engine->allocate_static_fields (klass, static_size);

  // Set the instance size for the class.  Note that first we round it
  // to the alignment required for this object; this keeps us in sync
  // with our current ABI.
  instance_size = ROUND (instance_size, max_align);
  klass->size_in_bytes = instance_size;
}

// This takes the class to state JV_STATE_LINKED.  The class lock must
// be held when calling this.
void
_Jv_Linker::ensure_class_linked (jclass klass)
{
  if (klass->state >= JV_STATE_LINKED)
    return;

  int state = klass->state;
  try
    {
      // Short-circuit, so that mutually dependent classes are ok.
      klass->state = JV_STATE_LINKED;

      _Jv_Constants *pool = &klass->constants;

      // Resolve class constants first, since other constant pool
      // entries may rely on these.
      for (int index = 1; index < pool->size; ++index)
	{
	  if (pool->tags[index] == JV_CONSTANT_Class)
	    resolve_pool_entry (klass, index);
	}

#if 0  // Should be redundant now
      // If superclass looks like a constant pool entry,
      // resolve it now.
      if ((uaddr) klass->superclass < (uaddr) pool->size)
	klass->superclass = pool->data[(int) klass->superclass].clazz;

      // Likewise for interfaces.
      for (int i = 0; i < klass->interface_count; i++)
	{
	  if ((uaddr) klass->interfaces[i] < (uaddr) pool->size)
	    klass->interfaces[i]
	      = pool->data[(int) klass->interfaces[i]].clazz;
	}
#endif

      // Resolve the remaining constant pool entries.
      for (int index = 1; index < pool->size; ++index)
	{
	  if (pool->tags[index] == JV_CONSTANT_String)
	    {
	      jstring str;

	      str = _Jv_NewStringUtf8Const (pool->data[index].utf8);
	      pool->data[index].o = str;
	      pool->tags[index] |= JV_CONSTANT_ResolvedFlag;
	    }
	}

      if (klass->engine->need_resolve_string_fields())
	{
	  jfieldID f = JvGetFirstStaticField (klass);
	  for (int n = JvNumStaticFields (klass); n > 0; --n)
	    {
	      int mod = f->getModifiers ();
	      // If we have a static String field with a non-null initial
	      // value, we know it points to a Utf8Const.
	      resolve_field(f, klass->loader);
	      if (f->getClass () == &java::lang::String::class$
		  && (mod & java::lang::reflect::Modifier::STATIC) != 0)
		{
		  jstring *strp = (jstring *) f->u.addr;
		  if (*strp)
		    *strp = _Jv_NewStringUtf8Const ((_Jv_Utf8Const *) *strp);
		}
	      f = f->getNextField ();
	    }
	}

      klass->notifyAll ();

      _Jv_PushClass (klass);
    }
  catch (java::lang::Throwable *t)
    {
      klass->state = state;
      throw t;
    }
}

// This ensures that symbolic superclass and superinterface references
// are resolved for the indicated class.  This must be called with the
// class lock held.
void
_Jv_Linker::ensure_supers_installed (jclass klass)
{
  resolve_class_ref (klass, &klass->superclass);
  // An interface won't have a superclass.
  if (klass->superclass)
    wait_for_state (klass->superclass, JV_STATE_LOADING);

  for (int i = 0; i < klass->interface_count; ++i)
    {
      resolve_class_ref (klass, &klass->interfaces[i]);
      wait_for_state (klass->interfaces[i], JV_STATE_LOADING);
    }
}

// This adds missing `Miranda methods' to a class.
void
_Jv_Linker::add_miranda_methods (jclass base, jclass iface_class)
{
  // Note that at this point, all our supers, and the supers of all
  // our superclasses and superinterfaces, will have been installed.

  for (int i = 0; i < iface_class->interface_count; ++i)
    {
      jclass interface = iface_class->interfaces[i];

      for (int j = 0; j < interface->method_count; ++j)
	{
 	  _Jv_Method *meth = &interface->methods[j];
	  // Don't bother with <clinit>.
	  if (meth->name->data[0] == '<')
	    continue;
	  _Jv_Method *new_meth = _Jv_LookupDeclaredMethod (base, meth->name,
							   meth->signature);
	  if (! new_meth)
	    {
	      // We assume that such methods are very unlikely, so we
	      // just reallocate the method array each time one is
	      // found.  This greatly simplifies the searching --
	      // otherwise we have to make sure that each such method
	      // found is really unique among all superinterfaces.
	      int new_count = base->method_count + 1;
	      _Jv_Method *new_m
		= (_Jv_Method *) _Jv_AllocBytes (sizeof (_Jv_Method)
						 * new_count);
	      memcpy (new_m, base->methods,
		      sizeof (_Jv_Method) * base->method_count);

	      // Add new method.
	      new_m[base->method_count] = *meth;
	      new_m[base->method_count].index = (_Jv_ushort) -1;
	      new_m[base->method_count].accflags
		|= java::lang::reflect::Modifier::INVISIBLE;

	      base->methods = new_m;
	      base->method_count = new_count;
	    }
	}

      add_miranda_methods (base, interface);
    }
}

// This ensures that the class' method table is "complete".  This must
// be called with the class lock held.
void
_Jv_Linker::ensure_method_table_complete (jclass klass)
{
  if (klass->vtable != NULL || klass->isInterface())
    return;
  // A class might have so-called "Miranda methods".  This is a method
  // that is declared in an interface and not re-declared in an
  // abstract class.  Some compilers don't emit declarations for such
  // methods in the class; this will give us problems since we expect
  // a declaration for any method requiring a vtable entry.  We handle
  // this here by searching for such methods and constructing new
  // internal declarations for them.  Note that we do this
  // unconditionally, and not just for abstract classes, to correctly
  // account for cases where a class is modified to be concrete and
  // still incorrectly inherits an abstract method.
  add_miranda_methods (klass, klass);
}

// Verify a class.  Must be called with class lock held.
void
_Jv_Linker::verify_class (jclass klass)
{
  klass->engine->verify(klass);
}

// FIXME: mention invariants and stuff.
void
_Jv_Linker::wait_for_state (jclass klass, int state)
{
  if (klass->state >= state)
    return;

  JvSynchronize sync (klass);

  // This is similar to the strategy for class initialization.  If we
  // already hold the lock, just leave.
  java::lang::Thread *self = java::lang::Thread::currentThread();
  while (klass->state <= state
	 && klass->thread 
	 && klass->thread != self)
    klass->wait ();

  java::lang::Thread *save = klass->thread;
  klass->thread = self;

  try
    {
      if (state >= JV_STATE_LOADING && klass->state < JV_STATE_LOADING)
	{
	  ensure_supers_installed (klass);
	  klass->set_state(JV_STATE_LOADING);
	}

      if (state >= JV_STATE_LOADED && klass->state < JV_STATE_LOADED)
	{
	  ensure_method_table_complete (klass);
	  klass->set_state(JV_STATE_LOADED);
	}

      if (state >= JV_STATE_PREPARED && klass->state < JV_STATE_PREPARED)
	{
	  ensure_fields_laid_out (klass);
	  make_vtable (klass);
	  layout_interface_methods (klass);
	  prepare_constant_time_tables (klass);
	  klass->set_state(JV_STATE_PREPARED);
	}

      if (state >= JV_STATE_LINKED && klass->state < JV_STATE_LINKED)
	{
	  ensure_class_linked (klass);
	  link_exception_table (klass);
	  link_symbol_table (klass);
	  klass->set_state(JV_STATE_LINKED);
	}
    }
  catch (java::lang::Throwable *exc)
    {
      klass->thread = save;
      klass->set_state(JV_STATE_ERROR);
      throw exc;
    }

  klass->thread = save;

  if (klass->state == JV_STATE_ERROR)
    throw new java::lang::LinkageError;
}
