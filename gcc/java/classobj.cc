// Create a Class instance.

// Copyright (C) 2004, 2005 Free Software Foundation, Inc.
//
// This file is part of GCC.
//
// GCC is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
//
// GCC is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.

#include "java/glue.hh"

record_creator::record_creator (tree record_type)
  : the_class (record_type)
{
  constructor = build_constructor (record_type, NULL_TREE);

  field_class = the_class;
  field_iterator = TYPE_FIELDS (field_class);

  while (DECL_NAME (field_iterator) == NULL_TREE)
    {
      field_class = TREE_TYPE (field_iterator);
      field_iterator = TYPE_FIELDS (field_class);
    }
}

record_creator::~record_creator ()
{
  assert (field_iterator == NULL_TREE);
  assert (constructor == NULL_TREE);
}

void
record_creator::set_field (const char *name, tree value)
{
  assert (! strcmp (IDENTIFIER_POINTER (DECL_NAME (field_iterator)), name));
  CONSTRUCTOR_ELTS (constructor) = tree_cons (field_iterator, value,
					      CONSTRUCTOR_ELTS (constructor));
  field_iterator = TREE_CHAIN (field_iterator);
  if (field_iterator == NULL_TREE
      && field_class != the_class)
    {
      // Now search downward from the most derived class to the parent
      // of the base class over which we just iterated.
      tree search = the_class;
      while (search != NULL_TREE)
	{
	  tree parent = TREE_TYPE (TYPE_FIELDS (search));
	  if (parent == field_class)
	    break;
	  search = parent;
	}
      assert (search != NULL_TREE);
      assert (search != field_class);
      field_class = search;
      // Make sure to skip over the super-class field.
      field_iterator = TREE_CHAIN (TYPE_FIELDS (field_class));
    }
}

tree
record_creator::finish_record ()
{
  CONSTRUCTOR_ELTS (constructor) = nreverse (CONSTRUCTOR_ELTS (constructor));
  tree result = constructor;
  constructor = NULL_TREE;
  return result;
}



tree
class_object_creator::make_decl (tree type, tree value)
{
  tree decl = build_decl (VAR_DECL, builtins->get_symbol (), type);
  DECL_INITIAL (decl) = value;
  TREE_STATIC (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;
  DECL_IGNORED_P (decl) = 1;
  rest_of_decl_compilation (decl, 1, 0);

  return build1 (ADDR_EXPR, build_pointer_type (type), decl);
}

tree
class_object_creator::create_one_field_record (model_field *field)
{
  record_creator inst (type_field);

  tree fdecl = builtins->map_field (field);

  inst.set_field ("name", builtins->map_utf8const (field->get_name ()));
  // FIXME: ABI difference here.
  inst.set_field ("type",
		  builtins->map_utf8const (field->type ()->get_descriptor ()));
  inst.set_field ("accflags", build_int_cst (type_jint,
					     field->get_modifiers ()));
  inst.set_field ("bsize", TYPE_SIZE_UNIT (TREE_TYPE (fdecl)));

  tree value;
  tree ufield = TYPE_FIELDS (type_field_info_union);
  if (field->static_p ())
    {
      ufield = TREE_CHAIN (ufield);
      value = build_address_of (fdecl);
    }
  else
    value = byte_position (fdecl);

  tree info = build_constructor (type_field_info_union,
				 build_tree_list (ufield, value));
  inst.set_field ("info", info);

  return inst.finish_record ();
}

tree
class_object_creator::create_field_array (model_class *real_class,
					  int &num_fields,
					  int &num_static_fields)
{
  std::list<ref_field> fields = real_class->get_fields ();

  num_fields = 0;
  num_static_fields = 0;
  if (fields.size () == 0)
    return null_pointer_node;

  int index = 0;
  tree field_array = NULL_TREE;
  for (std::list<ref_field>::const_iterator i = fields.begin ();
       i != fields.end ();
       ++i, ++index)
    {
      tree elt = create_one_field_record ((*i).get ());
      field_array = tree_cons (build_int_cst (type_jint, index),
			       elt, field_array);
      if ((*i)->static_p ())
	++num_static_fields;
      else
	++num_fields;
    }

  field_array = nreverse (field_array);

  tree fa_type
    = build_array_type (type_field,
			build_index_type (build_int_cst (type_jint,
							 index - 1)));
  return make_decl (fa_type, build_constructor (fa_type, field_array));
}

tree
class_object_creator::create_method_throws (model_method *method)
{
  std::list<ref_forwarding_type> throw_list = method->get_throws ();

  if (throw_list.empty ())
    return null_pointer_node;

  tree cons_list = tree_cons (NULL_TREE, null_pointer_node, NULL_TREE);
  for (std::list<ref_forwarding_type>::const_iterator i = throw_list.begin ();
       i != throw_list.end ();
       ++i)
    {
      tree utf = builtins->map_utf8const ((*i)->type ()->get_descriptor ());
      cons_list = tree_cons (NULL_TREE, utf, cons_list);
    }

  tree type
    = build_array_type (type_utf8const_ptr,
			build_index_type (build_int_cst (type_jint,
							 throw_list.size () + 1)));
  cons_list = build_constructor (type, cons_list);

  return make_decl (type, cons_list);
}

tree
class_object_creator::create_one_method_record (model_method *method)
{
  record_creator inst (type_method);
  tree mdecl = builtins->map_method (method);
  inst.set_field ("name", builtins->map_utf8const (method->get_name()));
  inst.set_field ("signature",
		  builtins->map_utf8const (method->get_descriptor ()));
  inst.set_field ("accflags",
		  build_int_cst (type_jushort, method->get_modifiers ()));
  inst.set_field ("index", integer_minus_one_node); // FIXME
  inst.set_field ("ncode", build_address_of (mdecl));
  inst.set_field ("throws", create_method_throws (method));
  return inst.finish_record ();
}

tree
class_object_creator::create_method_array (model_class *real_class,
					   int &num_methods)
{
  std::list<ref_method> methods = real_class->get_methods ();
  num_methods = methods.size ();
  if (num_methods == 0)
    return null_pointer_node;

  tree method_array = NULL_TREE;
  int index = 0;
  for (std::list<ref_method>::const_iterator i = methods.begin ();
       i != methods.end ();
       ++i, ++index)
    {
      tree elt = create_one_method_record ((*i).get ());
      method_array = tree_cons (build_int_cst (type_jint, index),
				elt, method_array);
    }

  method_array = nreverse (method_array);

  tree ma_type
    = build_array_type (type_method,
			build_index_type (build_int_cst (type_jint,
							 num_methods - 1)));
  return make_decl (ma_type, build_constructor (ma_type, method_array));
}

void
class_object_creator::create_index_table (const std::vector<model_element *> &table,
					  tree &result_table,
					  tree &result_syms)
{
  tree result_list = NULL_TREE;

  if (table.empty ())
    {
      result_table = null_pointer_node;
      result_syms = null_pointer_node;
      return;
    }

  for (std::vector<model_element *>::const_iterator i = table.begin ();
       i != table.end ();
       ++i)
    {
      // We can be looking at a field or a method.
      // FIXME: it would be nice if we could use IMember; it would
      // need name and descriptor accessors.
      std::string class_desc, name, descriptor;
      if (dynamic_cast<model_field *> (*i))
	{
	  model_field *field = assert_cast<model_field *> (*i);
	  class_desc = field->get_declaring_class ()->get_descriptor ();
	  name = field->get_name ();
	  descriptor = field->type ()->get_descriptor ();
	}
      else
	{
	  model_method *method = assert_cast<model_method *> (*i);
	  class_desc = method->get_declaring_class ()->get_descriptor ();
	  name = method->get_name ();
	  descriptor = method->get_descriptor ();
	}

      tree class_tree = builtins->map_utf8const (class_desc);
      tree name_tree = builtins->map_utf8const (name);
      tree desc_tree = builtins->map_utf8const (descriptor);

      record_creator item (type_method_symbol);
      item.set_field ("class_name", class_tree);
      item.set_field ("name", name_tree);
      item.set_field ("signature", desc_tree);
      tree item_tree = item.finish_record ();

      result_list = tree_cons (NULL_TREE, item_tree, result_list);
    }

  tree type
    = build_array_type (type_method_symbol,
			build_index_type (build_int_cst (type_jint, table.size ())));
  result_syms = make_decl (type, result);

  tree symtype
    = build_array_type (ptr_type_node,
			build_index_type (build_int_cst (type_jint,
							 table.size ())));
  // FIXME: we need a decl for this somewhere else so that the ABI can
  // emit references to it...
  result_table = make_decl (symtype, NULL_TREE);
}

void
class_object_creator::handle_interfaces (model_class *real_class,
					 tree &interfaces,
					 tree &iface_len)
{
  std::list<ref_forwarding_type> ifaces (real_class->get_interfaces ());
  int len = 0;

  if (ifaces.empty ())
    interfaces = null_pointer_node;
  else
    {
      tree result = NULL_TREE;
      for (std::list<ref_forwarding_type>::const_iterator i = ifaces.begin ();
	   i != ifaces.end ();
	   ++i)
	{
	  ++len;
	  gcj_abi *abi = builtins->find_abi ();
	  tree one_iface = abi->build_class_reference (builtins, klass,
						       (*i)->type ());
	  result = tree_cons (NULL_TREE, one_iface, result);
	}
      result = nreverse (result);

      // Make a new type which is an array of 'jclass' of the
      // appropriate length.
      tree type_index = build_index_type (build_int_cst (sizetype, len - 1));
      tree type_interface_array = build_array_type (type_class_ptr,
						    type_index);

      interfaces = make_decl (type_interface_array, result);
    }

  iface_len = build_int_cst (type_jshort, len);

}

tree
class_object_creator::create_constants ()
{
  const std::vector<aot_class::pool_entry> &pool (klass->get_constant_pool ());

  if (pool.empty ())
    return null_pointer_node;

  record_creator inst (type_constants);
  inst.set_field ("size", build_int_cst (type_juint, pool.size ()));

  tree type_tags
    = build_array_type (type_jbyte,
			build_index_type (build_int_cst (type_jint,
							 pool.size ())));
  tree type_data
    = build_array_type (ptr_type_node,
			build_index_type (build_int_cst (type_jint,
							 pool.size ())));

  tree tags_list = NULL_TREE;
  tree data_list = NULL_TREE;
  for (std::vector<aot_class::pool_entry>::const_iterator i = pool.begin ();
       i != pool.end ();
       ++i)
    {
      tags_list = tree_cons (NULL_TREE, build_int_cst (type_jbyte, (*i).tag),
			     tags_list);
      data_list = tree_cons (NULL_TREE, builtins->map_utf8const ((*i).value),
			     data_list);
    }

  tags_list = nreverse (tags_list);
  data_list = nreverse (data_list);

  inst.set_field ("tags", make_decl (type_tags, tags_list));
  inst.set_field ("data", make_decl (type_data, data_list));

  return inst.finish_record ();
}

void
class_object_creator::create_class_instance (tree class_tree)
{
  assert (TREE_CODE (class_tree) == RECORD_TYPE);

  model_class *real_class = klass->get ();
  gcj_abi *abi = builtins->find_abi ();
  record_creator inst (type_class);

  // First the fields from Object.
  inst.set_field ("vtable",
		  abi->get_vtable (builtins,
				   global->get_compiler ()->java_lang_Class ()));
  if (! flag_hash_synchronization)
    inst.set_field ("sync_info", null_pointer_node);

  // Now fields from Class.
  inst.set_field ("next_or_version", gcj_abi_version);
  inst.set_field ("name",
		  builtins->map_utf8const (real_class->get_fully_qualified_name ()));

  int mods = real_class->get_modifiers ();
  // Inner classes have modifiers like top-level classes, where the
  // only valid values are public and package-private.
  if ((mods & ACC_ACCESS) != 0 && (mods & ACC_ACCESS) != ACC_PUBLIC)
    mods &= ~ACC_ACCESS;
  inst.set_field ("accflags", build_int_cst (type_jushort, mods));

  tree super_tree = null_pointer_node;
  model_class *super = real_class->get_superclass ();
  if (real_class->interface_p ())
    super = global->get_compiler ()->java_lang_Object ();
  if (super)
    super_tree = abi->build_class_reference (builtins, klass, super);
  inst.set_field ("superclass", super_tree);

  inst.set_field ("constants", create_constants ());

  int method_len;
  tree methods = create_method_array (real_class, method_len);
  inst.set_field ("methods", methods);
  inst.set_field ("method_count", build_int_cst (type_jshort, method_len));
  inst.set_field ("vtable_method_count",
		  build_int_cst (type_jshort,
				 TREE_VEC_LENGTH (BINFO_VTABLE (TYPE_BINFO (class_tree)))));

  int num_fields, num_static_fields;
  tree field_array = create_field_array (real_class, num_fields,
					 num_static_fields);

  inst.set_field ("fields", field_array);
  inst.set_field ("size_in_bytes", abi->get_size_in_bytes (class_tree));
  inst.set_field ("field_count", build_int_cst (type_jshort, num_fields));
  inst.set_field ("static_field_count",
		  build_int_cst (type_jshort, num_static_fields));

  inst.set_field ("dtable", abi->get_vtable (builtins, klass->get (), true));

  tree table, syms;
  create_index_table (klass->get_otable (), table, syms);
  inst.set_field ("otable", table);
  inst.set_field ("otable_syms", syms);

  create_index_table (klass->get_atable (), table, syms);
  inst.set_field ("atable", table);
  inst.set_field ("atable_syms", syms);

  create_index_table (klass->get_itable (), table, syms);
  inst.set_field ("itable", table);
  inst.set_field ("itable_syms", syms);

  inst.set_field ("catch_classes", null_pointer_node);  // FIXME

  tree interfaces, interface_count;
  handle_interfaces (real_class, interfaces, interface_count);
  inst.set_field ("interfaces", interfaces);
  inst.set_field ("loader", null_pointer_node);
  inst.set_field ("interface_count", interface_count);

  inst.set_field ("state", build_int_cst (type_jbyte,
					  abi->get_class_state ()));
  inst.set_field ("thread", null_pointer_node);
  inst.set_field ("depth", integer_zero_node);
  inst.set_field ("ancestors", null_pointer_node);
  inst.set_field ("idt", null_pointer_node);
  inst.set_field ("arrayclass", null_pointer_node);
  inst.set_field ("protectionDomain", null_pointer_node);
  inst.set_field ("assertion_table", null_pointer_node);  // FIXME
  inst.set_field ("hack_signers", null_pointer_node);
  inst.set_field ("chain", null_pointer_node);
  inst.set_field ("aux_info", null_pointer_node);
  inst.set_field ("engine", null_pointer_node);

  tree init = inst.finish_record ();

  result = builtins->map_class_object (klass->get ());
  DECL_INITIAL (result) = init;
  rest_of_decl_compilation (result, 1, 0);

  result = build1 (ADDR_EXPR, type_class_ptr, result);
}
